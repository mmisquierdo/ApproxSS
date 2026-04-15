#include "approximate-buffer.h"

//WAS LOCKED
ApproximateBuffer::ApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes, const InjectionConfigurationReference& injectorCfg) : 
	SizedRange(bufferRange, dataSizeInBytes),
	m_id(id),
	//m_dataSizeInBytes(dataSizeInBytes),	
	m_minimumReadBackupSize(static_cast<size_t>(std::ceil(static_cast<double>(injectorCfg.GetBitDepth()) / static_cast<double>(BYTE_SIZE)))),
	//m_creationPeriod(creationPeriod),
	m_isActive(1),

	#if DISTANCE_BASED_FAULT_INJECTOR
		m_faultInjector(injectorCfg, dataSizeInBytes),
	#else
		m_faultInjector(injectorCfg),
	#endif

	m_periodLog(creationPeriod, m_faultInjector),
	m_bufferLogs()
{

	if (this->m_faultInjector.GetBitDepth() > (this->m_dataSizeInBytes * BYTE_SIZE)) {
		std::cerr << "ApproxSS Error: Bit Depth (" << injectorCfg.GetBitDepth() << "bits in Configuration " << injectorCfg.GetConfigurationId() << ") greater than Data Size (" << (this->m_dataSizeInBytes * BYTE_SIZE) << "bits in Buffer " << this->m_id << ")" << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	if (this->m_initialAddress > this->m_finalAddress) {
		std::cerr << "ApproxSS Error: On Buffer " << this->m_id << ".  Initial address (" << ((size_t) this->m_initialAddress) << ") must be less than final address (" << ((size_t) this->m_finalAddress)  << ")" << std::endl; //static_cast<size_t>
		PIN_ExitProcess(EXIT_FAILURE);
	}

	if (this->size() < this->m_dataSizeInBytes) {
		std::cerr << "ApproxSS Error: On Buffer " << this->m_id << ". Buffer Size (" << this->size() << ") must be greater or equal to Data Size (" << this->m_dataSizeInBytes << ")" << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	ApproximateBuffer::InitializeRecordsAndBackups(creationPeriod);

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//MUST LOCK
void ApproximateBuffer::InitializeRecordsAndBackups(const uint64_t period) {
	#if ENABLE_PASSIVE_INJECTION
		#if !DISTANCE_BASED_FAULT_INJECTOR
			using namespace BorrowedMemory;
			const LastAccessPeriodPool::iterator accessIt = g_lastAccessPeriodPool.find(this->GetNumberOfElements());
			if (accessIt != g_lastAccessPeriodPool.cend()) {
				this->m_lastAccessPeriod = std::unique_ptr<uint64_t[]>(accessIt->second.release());
				g_lastAccessPeriodPool.erase(accessIt);
			

				#if CAUTIOUS_LASTACCESSPERIOD_TAKEOVER
					std::fill_n(this->m_lastAccessPeriod.get(), this->GetNumberOfElements(), period);
				#else
					if (this->m_lastAccessPeriod[0] != period && this->m_lastAccessPeriod[this->GetNumberOfElements()-1] != period) { // should be safe enough 
						std::fill_n(this->m_lastAccessPeriod.get(), this->GetNumberOfElements(), period);
					}
				#endif
			} else {
				//this->m_lastAccessPeriod = std::unique_ptr<uint64_t[]>((uint64_t*) std::malloc(this->GetNumberOfElements() * sizeof(uint64_t))); "not supported" by Pin 4.0
				//this->m_lastAccessPeriod = std::make_unique_for_overwrite<uint64_t[]>(this->GetNumberOfElements()); "not supported" by Pin 4.0, requires C++20
				
				this->m_lastAccessPeriod = std::unique_ptr<uint64_t[]>(new uint64_t[this->GetNumberOfElements()]);
				std::fill_n(this->m_lastAccessPeriod.get(), this->GetNumberOfElements(), period);
			}
		#else
			this->m_lastPassiveInjectionPeriod = period;
		#endif
	#endif
}

//MUST LOCK
void ApproximateBuffer::GiveAwayRecordsAndBackups(const bool giveAwayRecords) {
	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		if (giveAwayRecords) {
				BorrowedMemory::g_lastAccessPeriodPool.insert({this->GetNumberOfElements(), std::unique_ptr<uint64_t[]>(this->m_lastAccessPeriod.release())});
			} else {
				this->m_lastAccessPeriod.reset();
			}
	#endif
}

int64_t ApproximateBuffer::GetConfigurationId() const {
	return this->m_faultInjector.GetConfigurationId();
}

int64_t ApproximateBuffer::GetBufferId() const {
	return this->m_id;
}

//MUST LOCK
void ApproximateBuffer::CleanLogs() { //for some reason, just calling .clear will cause a segmentation fault
	for (BufferLogs::const_iterator it = this->m_bufferLogs.cbegin(); it != this->m_bufferLogs.cend(); ) {
		it = this->m_bufferLogs.erase(it);
	}
}

//WAS LOCKED
ApproximateBuffer::~ApproximateBuffer() {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	this->CleanLogs();

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//MUST LOCK
//AND m_isActive MUST BE CHECKED
void ApproximateBuffer::ReactivateBuffer(const uint64_t creationPeriod) {
	#if MULTIPLE_BER_CONFIGURATION
		this->m_faultInjector.ResetBerIndex(creationPeriod);
	#endif

	ApproximateBuffer::InitializeRecordsAndBackups(creationPeriod);

	//this->m_creationPeriod = creationPeriod;

	const BufferLogs::const_iterator it = this->m_bufferLogs.find(creationPeriod);
	if (it != this->m_bufferLogs.cend()) {
		this->m_bufferLogs.erase(it);
	} else {
		this->m_periodLog.ResetCounts(creationPeriod, this->m_faultInjector);
	}
}

//MUST LOCK
void ApproximateBuffer::StoreCurrentPeriodLog() {
	this->m_bufferLogs.emplace(this->m_periodLog.m_period, std::make_unique<PeriodLog>(this->m_periodLog, this->m_faultInjector.GetBitDepth()));
}

//WAS LOCKED
void ApproximateBuffer::NextPeriod(const uint64_t period) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	#if ENABLE_PASSIVE_INJECTION && DISTANCE_BASED_FAULT_INJECTOR 
		if (this->m_faultInjector.isInjectable(ErrorCategory::Passive)) {
			this->m_faultInjector.InjectFault(this->m_initialAddress, ErrorCategory::Passive, this->GetSoftwareBufferSSizeInBytes(), nullptr IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Passive)));
			this->m_lastPassiveInjectionPeriod = period;
		}
	#endif

	this->StoreCurrentPeriodLog();

	#if MULTIPLE_BER_CONFIGURATION
		this->m_faultInjector.AdvanceBerIndex();
	#endif

	this->m_periodLog.ResetCounts(period, this->m_faultInjector);

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

uint64_t ApproximateBuffer::GetCurrentPassiveBerMarker() const {
	return g_currentPeriod;
}

size_t ApproximateBuffer::GetTotalNecessaryReadBackupSize() const {
	return this->GetNumberOfElements() * this->m_minimumReadBackupSize;
}

//MUST LOCK
bool ApproximateBuffer::GetShouldInject(const size_t errorCat, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) const {
	return isThreadInjectionEnabled IF_PIN_LOCKED(&& isBufferInThread) && this->m_faultInjector.isInjectable(errorCat); 
}

#if ENABLE_PASSIVE_INJECTION
	#if LOG_FAULTS
		//MUST LOCK
		uint64_t* ApproximateBuffer::GetPassiveErrorsLogFromIterator(const BufferLogs::const_iterator& it) const {
			if (it != this->m_bufferLogs.cend()) {
				return it->second->GetErrorCountsByBit(ErrorCategory::Passive);
			} else {
				return this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Passive);
			}
		}

		//MUST LOCK
		void ApproximateBuffer::AdvanceBufferLogIterator(BufferLogs::const_iterator& it) const {
			if (it != this->m_bufferLogs.cend()) { //NOTE: map iterators are circular
				++it;
			}
		}
	#endif

	#if !DISTANCE_BASED_FAULT_INJECTOR
		//MUST LOCK
		void ApproximateBuffer::UpdateLastAccessPeriod(uint8_t const * const initialAddress, const uint32_t accessSize) {
			const size_t initialElementIndex = this->GetIndexFromAddress(initialAddress);
			const size_t elementCount = accessSize / this->m_dataSizeInBytes;

			std::fill_n(&m_lastAccessPeriod[initialElementIndex], elementCount, this->GetCurrentPassiveBerMarker());
		}

		//MUST LOCK
		void ApproximateBuffer::UpdateLastAccessPeriod(uint8_t const * const accessedAddress) {
			const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);
			this->UpdateLastAccessPeriod(elementIndex);
		}

		//MUST LOCK
		void ApproximateBuffer::UpdateLastAccessPeriod(const size_t elementIndex) {
			this->m_lastAccessPeriod[elementIndex] = this->GetCurrentPassiveBerMarker();
		}
	#endif

	//MUST LOCK
	void ApproximateBuffer::ApplyAllPassiveErrors() {
		#if !DISTANCE_BASED_FAULT_INJECTOR
			this->ApplyPassiveFault(this->m_initialAddress, this->m_finalAddress);
		#else
			if (this->GetCurrentPassiveBerMarker() != this->m_lastPassiveInjectionPeriod) {
				if (this->m_faultInjector.isInjectable(ErrorCategory::Passive)) {
					this->m_faultInjector.InjectFault(this->m_initialAddress, ErrorCategory::Passive, this->GetSoftwareBufferSSizeInBytes(), nullptr IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Passive)));
					this->m_lastPassiveInjectionPeriod = g_currentPeriod;
				}
			}
		#endif
	}

	#if !DISTANCE_BASED_FAULT_INJECTOR
		//MUST LOCK
		void ApproximateBuffer::ApplyPassiveFault(uint8_t * const initialAddress, uint8_t const * const finalAddress) {
			if (this->m_faultInjector.isInjectable(ErrorCategory::Passive)) {
				size_t elementIndex = this->GetIndexFromAddress(initialAddress);

				for (uint8_t* currentAddress = initialAddress; currentAddress < finalAddress; currentAddress += this->m_dataSizeInBytes, ++elementIndex) {					
					this->ApplyPassiveFault(elementIndex, currentAddress);
				}
			}
		}

		//MUST LOCK
		void ApproximateBuffer::ApplyPassiveFault(uint8_t * const accessedAddress) {
			if (this->m_faultInjector.isInjectable(ErrorCategory::Passive)) {
				const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);
				this->ApplyPassiveFault(elementIndex, accessedAddress);
			}
		}

		//MUST LOCK
		void ApproximateBuffer::ApplyPassiveFault(const size_t elementIndex, uint8_t * const accessedAddress) {
			const uint64_t currentMarker = this->GetCurrentPassiveBerMarker();
			
			#if OVERCHARGE_BER
				uint64_t& initialMarker = this->m_lastAccessPeriod[elementIndex];
				
				if (currentMarker > initialMarker) {
					const auto& ber = this->m_faultInjector.GetBer(ErrorCategory::Passive, initialMarker, currentMarker);

					if (ber || !MULTIPLE_BER_CONFIGURATION) {
						#if OVERCHARGE_FLIP_BACK
							this->m_faultInjector.InjectFaultOvercharged(accessedAddress, ber);
						#else
							this->m_faultInjector.InjectFault(accessedAddress, ber, nullptr);
						#endif
					}

					initialMarker = currentMarker;
				}
			#else
				uint64_t& initialMarker = this->m_lastAccessPeriod[elementIndex];

				#if LOG_FAULTS
					BufferLogs::const_iterator it = this->m_bufferLogs.find(initialMarker);
				#endif

				for (/**/; initialMarker < currentMarker; ++initialMarker) {
					#if LOG_FAULTS
						this->AdvanceBufferLogIterator(it);
						uint64_t* const passiveErrorCount = this->GetPassiveErrorsLogFromIterator(it);
					#endif

					#if MULTIPLE_BER_CONFIGURATION
						const auto& ber = this->m_faultInjector.GetBer(ErrorCategory::Passive, this->m_faultInjector.GetBerIndexFromPeriod(initialMarker));
					#else
						const auto& ber = this->m_faultInjector.GetBer(ErrorCategory::Passive);
					#endif

					if (ber || !MULTIPLE_BER_CONFIGURATION) { //if MULTIPLE_BER_CONFIGURATION is false, the check is optimized away
						this->m_faultInjector.InjectFault(accessedAddress, ber, nullptr IF_COMMA_LOGGING_FAULTS(passiveErrorCount));
					}
				}
			#endif
		}
	#endif
#endif

void ApproximateBuffer::WriteLogHeaderToFile(std::ofstream& outputLog, const std::string& basePadding /*= ""*/) const {
	const std::string padding = basePadding + '\t';
	outputLog << basePadding << "Buffer {" << std::endl;
	outputLog << padding << "Id: " << this->m_id << std::endl;
	outputLog << padding << "Initial Address: " << (size_t) this->m_initialAddress << std::endl;	//static_cast<size_t>
	outputLog << padding << "Final Address: " << (size_t) this->m_finalAddress << std::endl;				//static_cast<size_t>
	outputLog << padding << "Configuration Id: " << this->m_faultInjector.GetConfigurationId() << std::endl;
	outputLog << padding << "Data Size (Bytes): " << this->m_dataSizeInBytes << std::endl;
	outputLog << padding << "Bit Depth: " << this->m_faultInjector.GetBitDepth() << std::endl;

	outputLog << padding << "Software/Proposed Size Bytes: " << this->GetSoftwareBufferSizeInBytes() << " / " << FormatDouble(CalculateProposedByteSize(this->GetNumberOfElements(), this->m_faultInjector.GetBitDepth())) << std::endl;
	outputLog << padding << "Elements: " << this->GetNumberOfElements() << std::endl << std::endl;
}

void ApproximateBuffer::WriteAccessLogToFile(std::ofstream& outputLog, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& totalTargetAccessesBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding) const {
	const std::string padding = basePadding + '\t';
	
	outputLog << std::endl;
	this->WriteLogHeaderToFile(outputLog, basePadding);

	uint64_t activePeriodsCount	= 0;
	std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> bufferAccessedBytes;
	std::fill_n(&(bufferAccessedBytes[0][0]), AccessPrecision::Size * AccessTypes::Size, 0);

	const InjectionConfigurationReference& referenceConfiguration = this->m_faultInjector.GetReferenceConfiguration();

	for (const auto& [_, bufLog] : this->m_bufferLogs) {
		++activePeriodsCount;
		bufLog->WriteAccessLogToFile(outputLog, this->m_faultInjector.GetBitDepth(), this->m_dataSizeInBytes, bufferAccessedBytes, totalTargetInjections, referenceConfiguration, padding);
	}

	if (!IsAccessBufferCountVirgin(bufferAccessedBytes)) {
		outputLog << padding << "Totals {" << std::endl;
		for (size_t i = 0; i < AccessPrecision::Size; ++i) {
			for (size_t j = 0; j < AccessTypes::Size; ++j) {
				if (bufferAccessedBytes[i][j]) {
					WriteAccessedBytesToFile(outputLog, this->m_faultInjector.GetBitDepth(), this->m_dataSizeInBytes, bufferAccessedBytes[i][j], AccessTypesNames[j], /*"Buffer " +*/ AccessPrecisionNames[i], padding + "\t");
					totalTargetAccessesBytes[i][j] += bufferAccessedBytes[i][j];
				}
			}
		}
		outputLog << padding << "}\n" << std::endl;
	}

	outputLog << padding << "Active Periods: " << activePeriodsCount << std::endl;

	outputLog << basePadding << "}" << std::endl;
}

void ApproximateBuffer::WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& totalTargetEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const std::string& basePadding) const {
	const std::string padding = basePadding + '\t';
	
	outputLog << std::endl;
	this->WriteLogHeaderToFile(outputLog, basePadding);

	uint64_t activePeriodsCount	= 0;
	std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> bufferEnergy;
	std::fill_n(bufferEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, -std::numeric_limits<double>::denorm_min());

	for (const auto& [_, bufLog] : this->m_bufferLogs) {
		++activePeriodsCount;
		bufLog->WriteEnergyLogToFile(outputLog, bufferEnergy, respectiveConsumptionProfile, this->m_faultInjector.GetBitDepth(), this->m_dataSizeInBytes, this->GetSoftwareBufferSizeInBytes(), padding);
	}

	if (WasEnergySpent(bufferEnergy)) {
		outputLog << padding << "Total {" << std::endl;

		WriteEnergyConsumptionToLogFile(outputLog, bufferEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding + '\t');
		//WriteEnergyConsumptionSavingsToLogFile(outputLog, bufferEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);
		AddEnergyConsumption(totalTargetEnergy, bufferEnergy);

		outputLog << padding << "}\n" << std::endl;
	}

	outputLog << padding << "Active Periods: " << activePeriodsCount << std::endl;

	outputLog << basePadding << "}" << std::endl;
}