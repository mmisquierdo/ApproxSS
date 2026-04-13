#include "period-log.h"

PeriodLog::PeriodLog(PeriodLog &other, const size_t bitDepth) {
	this->m_period = other.m_period;

	std::copy_n(&(other.m_accessedBytesCount[0][0]), AccessPrecision::Size * AccessTypes::Size, &(this->m_accessedBytesCount[0][0]));

	#if LOG_FAULTS
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			this->m_errorsCountsByBit[i] = std::make_unique<uint64_t[]>(bitDepth);
			std::copy_n(other.m_errorsCountsByBit[i].get(), bitDepth, this->m_errorsCountsByBit[i].get());
			std::swap(other.m_errorsCountsByBit[i], this->m_errorsCountsByBit[i]);
		}
	#endif

	#if MULTIPLE_BER_CONFIGURATION
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			this->m_berIndex[i] = other.m_berIndex[i];
		}
	#endif
}

PeriodLog::PeriodLog(const uint64_t period, const InjectionConfigurationLocal &injectorCfg) {
	#if LOG_FAULTS
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			this->m_errorsCountsByBit[i] = std::make_unique<uint64_t[]>(injectorCfg.GetBitDepth());
		}
	#endif

	this->ResetCounts(period, injectorCfg);
}

void PeriodLog::ResetCounts(const uint64_t period, const InjectionConfigurationLocal &injectorCfg) {
	this->m_period = period;
	std::fill_n(&(this->m_accessedBytesCount[0][0]), AccessPrecision::Size * AccessTypes::Size, 0);

	#if LOG_FAULTS
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			std::fill_n(this->m_errorsCountsByBit[i].get(), injectorCfg.GetBitDepth(), 0);
		}
	#endif

	#if MULTIPLE_BER_CONFIGURATION
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			this->m_berIndex[i] = injectorCfg.GetBerCurrentIndex(i);
		}
	#endif
}

void PeriodLog::IncreaseAccess(const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread), const size_t type, const size_t size /*in bytes*/) {
	#if PIN_LOCKED
		if (isBufferInThread) {
	#endif

	this->m_accessedBytesCount[isThreadInjectionEnabled][type] += size;

	#if PIN_LOCKED
		}
	#endif
}

bool PeriodLog::IsVirgin() const {
	return IsAccessBufferCountVirgin(this->m_accessedBytesCount);
}

#if MULTIPLE_BER_CONFIGURATION
	void PeriodLog::WriteBerIndexesToFile(std::ofstream &outputLog, const std::string &basePadding /*= ""*/) const {
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			outputLog << basePadding << ErrorCategoryNames[i] << " sub-BER index: " << this->m_berIndex[i] << std::endl;
		}
	}
#endif

#if LOG_FAULTS
	uint64_t* PeriodLog::GetErrorCountsByBit(const size_t errorCat) const {
		return this->m_errorsCountsByBit[errorCat].get();
	}

	void PeriodLog::WriteAndSumIndividualInjectionArray(std::ofstream &outputLog, const std::string errorType, const size_t bitDepth, uint64_t &bufferTotalInjected, uint64_t const *const injectedByBit, const std::string &basePadding /*= ""*/) const {
		const std::string padding = basePadding + '\t';

		std::ostringstream oss;

		oss << padding << errorType << " errors by bit {" << std::endl;

		uint64_t periodTotalInjected = 0;
		for (size_t i = 0; i < bitDepth; ++i) {
			if (injectedByBit[i]) {
				oss << padding << "\t" << i << ": " << injectedByBit[i] << std::endl;
			}
			periodTotalInjected += injectedByBit[i];
		}

		oss << padding << "}" << std::endl;

		oss << '\n' << padding << "Total: " << periodTotalInjected << std::endl;
		bufferTotalInjected += periodTotalInjected;

		oss << std::endl;

		if (periodTotalInjected) {
			outputLog << oss.str();
		}
	}
#endif

void PeriodLog::WriteAccessLogToFile(std::ofstream &outputLog, const size_t bitDepth, const size_t dataSizeInBytes, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> &bufferAccessedBytes, std::array<uint64_t, ErrorCategory::Size> &totalTargetInjections, const std::string &basePadding /*= ""*/) const {
	if (this->IsVirgin()) {
		return;
	}
	
	const std::string padding = basePadding + '\t';

	outputLog << basePadding << "Period Id: " << this->m_period << " {" << std::endl;

	for (size_t i = 0; i < AccessPrecision::Size; ++i) {
		for (size_t j = 0; j < AccessTypes::Size; ++j) {
			if (this->m_accessedBytesCount[i][j]) {
				WriteAccessedBytesToFile(outputLog, bitDepth, dataSizeInBytes, this->m_accessedBytesCount[i][j], AccessTypesNames[j], /*" Period " +*/ AccessPrecisionNames[i], padding);
				bufferAccessedBytes[i][j] += this->m_accessedBytesCount[i][j];
			}
		}
	}
	//outputLog << std::endl;

	#if MULTIPLE_BER_CONFIGURATION
		this->WriteBerIndexesToFile(outputLog, padding);
	#endif

	#if LOG_FAULTS
		outputLog << std::endl;
		outputLog << padding << "Injections {" << std::endl;
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			this->WriteAndSumIndividualInjectionArray(outputLog, ErrorCategoryNames[i], bitDepth, totalTargetInjections[i], this->GetErrorCountsByBit(i), padding);
		}
		outputLog << padding << "}" << std::endl;
	#endif

	outputLog << basePadding << "}" << std::endl;
	outputLog << std::endl;
}

void PeriodLog::CalculateEnergyConsumptionByErrorCategory(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &periodEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t consumptionTypeIndex, const size_t errorCat, const size_t softwareProcessedBytes) const {
	const bool NaN = (consumptionTypeIndex == ConsumptionType::Precise) && (!respectiveConsumptionProfile.HasReferenceValues());

	if (!NaN) {
		#if MULTIPLE_BER_CONFIGURATION
			const size_t tempBerIndex = this->m_berIndex[errorCat];
		#else
			const size_t tempBerIndex = 0;
		#endif

		const double energy = respectiveConsumptionProfile.EstimateEnergyConsumption(softwareProcessedBytes, bitDepth, dataSizeInBytes, consumptionTypeIndex, errorCat, tempBerIndex);
		
		periodEnergy[consumptionTypeIndex][errorCat] = energy;
	} else {
		periodEnergy[consumptionTypeIndex][errorCat] = -std::numeric_limits<double>::denorm_min();
	}
}

void PeriodLog::CalculatePeriodEnergyConsumption(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &periodEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t bufferSizeInBytes) const {
	for (size_t consumptionTypeIndex = 0; consumptionTypeIndex < ConsumptionType::Size; ++consumptionTypeIndex) {
		for (size_t accessType = 0; accessType < AccessTypes::Size; ++accessType) {
			this->CalculateEnergyConsumptionByErrorCategory(periodEnergy, respectiveConsumptionProfile, bitDepth, dataSizeInBytes, consumptionTypeIndex, accessType, this->m_accessedBytesCount[consumptionTypeIndex][accessType]);
		}

		#if ENABLE_PASSIVE_INJECTION
			this->CalculateEnergyConsumptionByErrorCategory(periodEnergy, respectiveConsumptionProfile, bitDepth, dataSizeInBytes, consumptionTypeIndex, ErrorCategory::Passive, bufferSizeInBytes);
		#endif
	}
}

void PeriodLog::WriteEnergyLogToFile(std::ofstream &outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &bufferEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t bufferSizeInBytes, const std::string &basePadding /*= ""*/) const {
	const std::string padding = basePadding + '\t';

	std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> periodEnergy;
	std::fill_n(periodEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, -std::numeric_limits<double>::denorm_min());

	this->CalculatePeriodEnergyConsumption(periodEnergy, respectiveConsumptionProfile, bitDepth, dataSizeInBytes, bufferSizeInBytes);

	if (WasEnergySpent(periodEnergy)) {
		outputLog << basePadding << "Period Id: " << this->m_period << " {" << std::endl;

		WriteEnergyConsumptionToLogFile(outputLog, periodEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);

		//WriteEnergyConsumptionSavingsToLogFile(outputLog, periodEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);

		AddEnergyConsumption(bufferEnergy, periodEnergy);

		outputLog << basePadding << "}" << std::endl;
		outputLog << std::endl;
	}
}

void WriteEnergyConsumptionToLogFile(std::ofstream &outputLog, const std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &energy, const bool hasReferenceValues, const bool checkNaN /*= true*/, const std::string &basePadding /*= ""*/) {
	//const std::string padding = basePadding + '\t';
	
	for (size_t consumptionTypeIndex = 0; consumptionTypeIndex < ConsumptionType::Size; ++consumptionTypeIndex) {
		for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
			if (WasEnergySpent(energy[consumptionTypeIndex][errorCat])) {
				WriteEnergyToFile(outputLog, energy[consumptionTypeIndex][errorCat], ErrorCategoryNames[errorCat], ConsumptionTypeNames[consumptionTypeIndex], basePadding);
			}
		}
	}
}

//void WriteEnergyConsumptionSavingsToLogFile(std::ofstream &outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &energy, const bool hasReferenceValues, const bool checkNaN /*= true*/, const std::string &basePadding /*= ""*/) {
/*	const std::string padding = basePadding + '\t';
	
	outputLog << basePadding << "ENERGY CONSUMPTION SAVINGS" << std::endl;
	for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
		outputLog << padding << ErrorCategoryNames[errorCat] << ": ";

		if ((checkNaN && !hasReferenceValues) || energy[ConsumptionType::Precise][errorCat] == 0) {
			outputLog << "NaN";
		} else {
			outputLog << (100 - ((energy[ConsumptionType::Approximate][errorCat] / energy[ConsumptionType::Precise][errorCat]) * 100)) << '%';
		}
		
		outputLog << std::endl;
	}
}*/

void AddEnergyConsumption(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& destination, const std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& source) {
	for (size_t consumptionTypeIndex = 0; consumptionTypeIndex < ConsumptionType::Size; ++consumptionTypeIndex) {
		for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
			if (WasEnergySpent(destination[consumptionTypeIndex][errorCat])) {
				if (WasEnergySpent(source[consumptionTypeIndex][errorCat])) {
					destination[consumptionTypeIndex][errorCat] += source[consumptionTypeIndex][errorCat];
				} else {
					//nothing
				}
			} else {
				if (WasEnergySpent(source[consumptionTypeIndex][errorCat])) {
					destination[consumptionTypeIndex][errorCat] = source[consumptionTypeIndex][errorCat];
				} else {
					//nothing
				}
			}
		}
	}
}

double CalculateProposedByteSize(const size_t elementCount, const size_t bitDepth) {
	return(static_cast<double>(elementCount) * bitDepth) / BYTE_SIZE;
}

bool IsAccessBufferCountVirgin(const std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& accessBuffer) {
	for (size_t i = 0; i < AccessPrecision::Size; ++i) {
		for (size_t j = 0; j < AccessTypes::Size; ++j) {
			if (accessBuffer[i][j] != 0) {
				return false;
			}
		}
	}
	
	return true;
}

bool WasEnergySpent(const double& energy) {
	return energy > 0;
}

bool WasEnergySpent(const std::array<double, ErrorCategory::Size> &energy) {
	for (size_t i = 0; i < ErrorCategory::Size; ++i) {
		if (WasEnergySpent(energy[i])) {
			return true;
		}
	}
	
	return false;
}

bool WasEnergySpent(const std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &energy) {
	for (size_t i = 0; i < ConsumptionType::Size; ++i) {
		if (WasEnergySpent(energy[i])) {
			return true;
		}
	}
	
	return false;
}

void WriteAccessedBytesToFile(std::ofstream &outputLog, const size_t bitDepth, const size_t dataSizeInBytes, const uint64_t accessedBytes, const std::string &accessedType, const std::string &accessScope, const std::string &padding /*= ""*/) {
	outputLog << padding << accessScope << " " << accessedType << " Software/Proposed Bytes: " << accessedBytes << " / " << FormatDouble(CalculateProposedByteSize(accessedBytes/dataSizeInBytes, bitDepth)) << std::endl;
}

void WriteEnergyToFile(std::ofstream &outputLog, const double energy , const std::string &errorCat, const std::string &consumptionType, const std::string &padding /*= ""*/) {
	outputLog << padding << consumptionType << " " << errorCat << ": ";

	if (WasEnergySpent(energy)) {
		outputLog << FormatDouble(energy) << "pJ";
	} else {
		outputLog << "NaN";
	}

	outputLog << std::endl;
}

std::string FormatDouble(const double value) {
    std::ostringstream oss;
    
    oss << std::fixed << std::setprecision(6) << value;
    std::string str = oss.str();
    
    if (str.find('.') != std::string::npos) {
        str.erase(str.find_last_not_of('0') + 1, std::string::npos);
        
        if (str.back() == '.') {
            str.pop_back();
        }
    }
    
    return str;
}