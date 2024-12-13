#include "period-log.h"

PeriodLog::PeriodLog(PeriodLog &other, const size_t bitDepth) {
	this->m_period = other.m_period;

	//std::copy_n(&(other.m_accessedBytesCount[0][0]), AccessPrecision::Size * AccessTypes::Size, &(this->m_accessedBytesCount[0][0]));

	for (const size_t i = 0; i < AccessTypes::Size; ++i) {
		this->m_accessedBytesCount[i] = other.m_accessedBytesCount[i];
	}

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
	//std::fill_n(&(this->m_accessedBytesCount[0][0]), AccessPrecision::Size * AccessTypes::Size, 0);
	for (const size_t i = 0; i < AccessTypes::Size; ++i) {
		this->m_accessedBytesCount[i].clear();
	}

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
	/*#if PIN_LOCKED
		if (isBufferInThread) {
	#endif

	this->m_accessedBytesCount[isThreadInjectionEnabled][type] += size;

	#if PIN_LOCKED
		}
	#endif*/

	this->m_accessedBytesCount[type][g_sequenceHash] += size;
}

/*bool PeriodLog::IsVirgin() const {
	for (size_t i = 0; i < AccessPrecision::Size; ++i) {
		for (size_t j = 0; j < AccessTypes::Size; ++j) {
			if (this->m_accessedBytesCount[i][j] != 0) {
				return false;
			}
		}
	}
	
	return true;
}*/

void PeriodLog::WriteBerIndexesToFile(std::ofstream &outputLog, const std::string &basePadding /*= ""*/) const {
	for (size_t i = 0; i < ErrorCategory::Size; ++i) {
		outputLog << basePadding << ErrorCategoryNames[i] << " sub-BER index: " <<
		#if MULTIPLE_BER_CONFIGURATION
			this->m_berIndex[i]
		#else
			0
		#endif
		<< std::endl;
	}
}

#if LOG_FAULTS
	uint64_t* PeriodLog::GetErrorCountsByBit(const size_t errorCat) const {
		return this->m_errorsCountsByBit[errorCat].get();
	}

	void PeriodLog::WriteAndSumIndividualInjectionArray(std::ofstream &outputLog, const std::string errorType, const size_t bitDepth, uint64_t &bufferTotalInjected, uint64_t const *const injectedByBit, const std::string &basePadding /*= ""*/) const {
		const std::string padding = basePadding + '\t';

		outputLog << padding << errorType << " errors injected by bit:" << std::endl;

		uint64_t periodTotalInjected = 0;
		for (size_t i = 0; i < bitDepth; ++i) {
			outputLog << padding << "\tBit " << i << ": " << injectedByBit[i] << std::endl;
			periodTotalInjected += injectedByBit[i];
		}

		outputLog << padding << "Period " << errorType << " injected errors: " << periodTotalInjected << std::endl;
		bufferTotalInjected += periodTotalInjected;

		outputLog << std::endl;
	}
#endif

void PeriodLog::WriteAccessLogToFile(std::ofstream &outputLog, const size_t bitDepth, const size_t dataSizeInBytes, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> &bufferAccessedBytes, std::array<uint64_t, ErrorCategory::Size> &totalTargetInjections, const std::string &basePadding /*= ""*/) const {
	const std::string padding = basePadding + '\t';

	outputLog << basePadding << "PERIOD START" << std::endl;
	outputLog << padding << "For the period: " << this->m_period << std::endl;

	/*for (size_t i = 0; i < AccessPrecision::Size; ++i) {
		for (size_t j = 0; j < AccessTypes::Size; ++j) {
			WriteAccessedBytesToFile(outputLog, bitDepth, dataSizeInBytes, this->m_accessedBytesCount[i][j], AccessTypesNames[j], "Period " + AccessPrecisionNames[i], padding);
			bufferAccessedBytes[i][j] += this->m_accessedBytesCount[i][j];
		}
	}*/
	for (size_t i = 0; i < AccessTypes::Size; ++i) {
			WriteAccessedBytesToFile(outputLog, bitDepth, dataSizeInBytes, this->m_accessedBytesCount[i], AccessTypesNames[i], "Period ", padding);
			bufferAccessedBytes[i][j] += this->m_accessedBytesCount[i][j];
	}

	outputLog << std::endl;

	/*this->WriteBerIndexesToFile(outputLog, padding);

	#if LOG_FAULTS
		outputLog << std::endl;
		outputLog << padding << "INJECTION COUNT START" << std::endl;
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			this->WriteAndSumIndividualInjectionArray(outputLog, ErrorCategoryNames[i], bitDepth, totalTargetInjections[i], this->GetErrorCountsByBit(i), padding);
		}
		outputLog << padding << "INJECTION COUNT END" << std::endl;
	#endif*/

	outputLog << basePadding << "PERIOD END" << std::endl;
	outputLog << std::endl;
}

void PeriodLog::CalculateEnergyConsumptionByErrorCategory(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &periodEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t consumptionTypeIndex, const size_t errorCat, const size_t softwareProcessedBytes) const {
	const bool NaN = (consumptionTypeIndex == ConsumptionType::Reference) && (!respectiveConsumptionProfile.HasReferenceValues());

	if (!NaN) {
		#if MULTIPLE_BER_CONFIGURATION
			const size_t tempBerIndex = this->m_berIndex[errorCat];
		#else
			const size_t tempBerIndex = 0;
		#endif

		const double energy = respectiveConsumptionProfile.EstimateEnergyConsumption(softwareProcessedBytes, bitDepth, dataSizeInBytes, consumptionTypeIndex, errorCat, tempBerIndex);
		
		periodEnergy[consumptionTypeIndex][errorCat] = energy;
	}
}

void PeriodLog::CalculatePeriodEnergyConsumption(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &periodEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t bufferSizeInBytes) const {
	//precise access
	/*for (size_t accessType = 0; accessType < AccessTypes::Size; ++accessType) {
			this->CalculateEnergyConsumptionByErrorCategory(periodEnergy, respectiveConsumptionProfile, bitDepth, dataSizeInBytes, ConsumptionType::Reference, accessType, this->m_accessedBytesCount[AccessPrecision::Precise][accessType]);
	}*/
	
	//approximate access
	/*for (size_t consumptionTypeIndex = 0; consumptionTypeIndex < ConsumptionType::Size; ++consumptionTypeIndex) {
		for (size_t accessType = 0; accessType < AccessTypes::Size; ++accessType) {
			this->CalculateEnergyConsumptionByErrorCategory(periodEnergy, respectiveConsumptionProfile, bitDepth, dataSizeInBytes, consumptionTypeIndex, accessType, this->m_accessedBytesCount[consumptionTypeIndex][accessType]);
		}

		#if ENABLE_PASSIVE_INJECTION
			this->CalculateEnergyConsumptionByErrorCategory(periodEnergy, respectiveConsumptionProfile, bitDepth, dataSizeInBytes, consumptionTypeIndex, ErrorCategory::Passive, bufferSizeInBytes);
		#endif
	}*/
}

void PeriodLog::WriteEnergyLogToFile(std::ofstream &outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &bufferEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t bufferSizeInBytes, const std::string &basePadding /*= ""*/) const {
	const std::string padding = basePadding + '\t';

	std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> periodEnergy;
	std::fill_n(periodEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, 0);

	this->CalculatePeriodEnergyConsumption(periodEnergy, respectiveConsumptionProfile, bitDepth, dataSizeInBytes, bufferSizeInBytes);

	outputLog << basePadding << "PERIOD START" << std::endl;
	outputLog << padding << "For the period: " << this->m_period << std::endl;

	WriteEnergyConsumptionToLogFile(outputLog, periodEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);

	//WriteEnergyConsumptionSavingsToLogFile(outputLog, periodEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);

	AddEnergyConsumption(bufferEnergy, periodEnergy);

	outputLog << basePadding << "PERIOD END" << std::endl;
	outputLog << std::endl;
}

void WriteEnergyConsumptionToLogFile(std::ofstream &outputLog, const std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &energy, const bool hasReferenceValues, const bool checkNaN /*= true*/, const std::string &basePadding /*= ""*/) {
	const std::string padding = basePadding + '\t';
	
	for (size_t consumptionTypeIndex = 0; consumptionTypeIndex < ConsumptionType::Size; ++consumptionTypeIndex) {
		outputLog << basePadding << ConsumptionTypeNames[consumptionTypeIndex] << " ENERGY CONSUMPTION" << std::endl;

		const bool NaN = checkNaN && ((consumptionTypeIndex == ConsumptionType::Reference) && !(hasReferenceValues));

		for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
			outputLog << padding << ErrorCategoryNames[errorCat] << ": ";

			if (NaN) {
				outputLog << "NaN";
			} else {
				outputLog << energy[consumptionTypeIndex][errorCat] << "pJ";
			}

			outputLog << std::endl;
		}
	}
	outputLog << std::endl;
}

//void WriteEnergyConsumptionSavingsToLogFile(std::ofstream &outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &energy, const bool hasReferenceValues, const bool checkNaN /*= true*/, const std::string &basePadding /*= ""*/) {
/*	const std::string padding = basePadding + '\t';
	
	outputLog << basePadding << "ENERGY CONSUMPTION SAVINGS" << std::endl;
	for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
		outputLog << padding << ErrorCategoryNames[errorCat] << ": ";

		if ((checkNaN && !hasReferenceValues) || energy[ConsumptionType::Reference][errorCat] == 0) {
			outputLog << "NaN";
		} else {
			outputLog << (100 - ((energy[ConsumptionType::Approximate][errorCat] / energy[ConsumptionType::Reference][errorCat]) * 100)) << '%';
		}
		
		outputLog << std::endl;
	}
}*/

void AddEnergyConsumption(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& destination, const std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& source) {
	for (size_t consumptionTypeIndex = 0; consumptionTypeIndex < ConsumptionType::Size; ++consumptionTypeIndex) {
		for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
			destination[consumptionTypeIndex][errorCat] += source[consumptionTypeIndex][errorCat];
		}
	}
}



void WriteAccessedBytesToFile(std::ofstream &outputLog, const size_t bitDepth, const size_t dataSizeInBytes, const std::map<int64_t, uint64_t>& accessedBytes, const std::string &accessedType, const std::string &accessScope, const std::string &padding /*= ""*/) {
	outputLog << padding << accessScope << " " << accessedType << " Software Implementation Bytes: " << std::endl; //<< accessedBytes << " / " << (accessedBytes * BYTE_SIZE) << std::endl;
	//outputLog << padding << accessScope << " " << accessedType << " Proposed Implementation Bytes/Bits: " << (((accessedBytes / dataSizeInBytes) * bitDepth) / BYTE_SIZE) << " / " << ((accessedBytes / dataSizeInBytes) * bitDepth) << std::endl;

	for (const auto& [hash, accessedCount] : accessedBytes) {
		
	}
}