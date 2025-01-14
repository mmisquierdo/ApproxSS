#include "approximate-buffer.h"

namespace BorrowedMemory {
	//#if LONG_TERM_BUFFER
		InjectionRecordPool g_injectionRecords;
		ReadBackupsPool g_readBackups;

		#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
			WriteSupportRecordPool g_writeSupportRecordPool;
		#endif
	//#endif

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		LastAccessPeriodPool g_lastAccessPeriodPool;
	#endif
}

//WAS LOCKED
ApproximateBuffer::ApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes, const InjectionConfigurationReference& injectorCfg) : 
	Range(bufferRange),
	m_id(id),
	m_dataSizeInBytes(dataSizeInBytes),	
	m_minimumReadBackupSize(static_cast<size_t>(std::ceil(static_cast<double>(injectorCfg.GetBitDepth()) / static_cast<double>(BYTE_SIZE)))),
	m_creationPeriod(creationPeriod),
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
			} else {
				this->m_lastAccessPeriod = std::unique_ptr<uint64_t[]>((uint64_t*) std::malloc(this->GetNumberOfElements() * sizeof(uint64_t)));
			}

			if (this->m_lastAccessPeriod[0] != period) {
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

	this->m_creationPeriod = creationPeriod;

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
		if (this->m_faultInjector.GetShouldGoOn(ErrorCategory::Passive)) {
			this->m_faultInjector.InjectFault(this->m_initialAddress, ErrorCategory::Passive, this->GetSoftwareBufferSSizeInBytes(), nullptr AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Passive)));
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

size_t ApproximateBuffer::GetSoftwareBufferSizeInBytes() const {
	return this->size();
}

ssize_t ApproximateBuffer::GetSoftwareBufferSSizeInBytes() const {
	return this->ssize();
}

size_t ApproximateBuffer::GetSoftwareBufferSizeInBits() const {
	return this->GetSoftwareBufferSizeInBytes() * BYTE_SIZE;
}

size_t ApproximateBuffer::GetNumberOfElements() const {
	return this->GetSoftwareBufferSizeInBytes() / this->m_dataSizeInBytes;
}

size_t ApproximateBuffer::GetImplementationBufferSizeInBits() const {
	return this->GetNumberOfElements() * this->m_faultInjector.GetBitDepth();
}

size_t ApproximateBuffer::GetTotalNecessaryReadBackupSize() const {
	return this->GetNumberOfElements() * this->m_minimumReadBackupSize;
}

//MUST LOCK
bool ApproximateBuffer::GetShouldInject(const size_t errorCat, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) const {
	return isThreadInjectionEnabled IF_PIN_LOCKED(&& isBufferInThread) && this->m_faultInjector.GetShouldGoOn(errorCat); 
}

size_t ApproximateBuffer::GetIndexFromAddress(uint8_t const * const address) const {
	return ((size_t) (address - this->m_initialAddress)) / this->m_dataSizeInBytes; //static_cast<size_t>
}

size_t ApproximateBuffer::GetAlignmentOffset(uint8_t const * const address) const {
	return static_cast<size_t>(address - this->m_initialAddress) % this->m_dataSizeInBytes;
}

bool ApproximateBuffer::IsMisaligned(uint8_t const * const address) const {
	return this->GetAlignmentOffset(address) != 0; 
}
bool ApproximateBuffer::IsIgnorableMisaligned(uint8_t const * const address, const uint32_t accessSize) const {
	return this->IsMisaligned(address) && accessSize < this->m_dataSizeInBytes;
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
				if (this->m_faultInjector.GetShouldGoOn(ErrorCategory::Passive)) {
					this->m_faultInjector.InjectFault(this->m_initialAddress, ErrorCategory::Passive, this->GetSoftwareBufferSSizeInBytes(), nullptr AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Passive)));
					this->m_lastPassiveInjectionPeriod = g_currentPeriod;
				}
			}
		#endif
	}

	#if !DISTANCE_BASED_FAULT_INJECTOR
		//MUST LOCK
		void ApproximateBuffer::ApplyPassiveFault(uint8_t * const initialAddress, uint8_t const * const finalAddress) {
			if (this->m_faultInjector.GetShouldGoOn(ErrorCategory::Passive)) {
				size_t elementIndex = this->GetIndexFromAddress(initialAddress);

				for (uint8_t* currentAddress = initialAddress; currentAddress < finalAddress; currentAddress += this->m_dataSizeInBytes, ++elementIndex) {					
					this->ApplyPassiveFault(elementIndex, currentAddress);
				}
			}
		}

		//MUST LOCK
		void ApproximateBuffer::ApplyPassiveFault(uint8_t * const accessedAddress) {
			if (this->m_faultInjector.GetShouldGoOn(ErrorCategory::Passive)) {
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
						this->m_faultInjector.InjectFault(accessedAddress, ber, nullptr AND_LOG_ARGUMENT(passiveErrorCount));
					}
				}
			#endif
		}
	#endif
#endif

void ApproximateBuffer::WriteLogHeaderToFile(std::ofstream& outputLog, const std::string& basePadding /*= ""*/) const {
	const std::string padding = basePadding + '\t';
	outputLog << basePadding << "BUFFER START" << std::endl;
	outputLog << padding << "Buffer Id: " << this->m_id << std::endl;
	outputLog << padding << "Initial Address: " << (size_t) this->m_initialAddress << std::endl;	//static_cast<size_t>
	outputLog << padding << "Final Address: " << (size_t) this->m_finalAddress << std::endl;				//static_cast<size_t>
	outputLog << padding << "Configuration Id: " << this->m_faultInjector.GetConfigurationId() << std::endl;
	outputLog << padding << "Data Size (Bytes): " << this->m_dataSizeInBytes << std::endl;
	outputLog << padding << "Bit Depth: " << this->m_faultInjector.GetBitDepth() << std::endl;

	outputLog << padding << "Buffer Software Implementation Size Bytes/Bits: " << this->GetSoftwareBufferSizeInBytes() << " / " << this->GetSoftwareBufferSizeInBits() << std::endl;
	outputLog << padding << "Buffer Proposed Implementation Size Bytes/Bits: " << (this->GetImplementationBufferSizeInBits() / BYTE_SIZE) << " / " << this->GetImplementationBufferSizeInBits() << std::endl;
	outputLog << padding << "Buffer Elements: " << this->GetNumberOfElements() << std::endl << std::endl;
}
 

void ApproximateBuffer::WriteAccessLogToFile(std::ofstream& outputLog, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& totalTargetAccessesBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding) const {
	const std::string padding = basePadding + '\t';
	
	outputLog << std::endl;
	this->WriteLogHeaderToFile(outputLog, basePadding);

	uint64_t activePeriodsCount	= 0;
	std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> bufferAccessedBytes;
	std::fill_n(&(bufferAccessedBytes[0][0]), AccessPrecision::Size * AccessTypes::Size, 0);

	for (const auto& [_, bufLog] : this->m_bufferLogs) {
		++activePeriodsCount;
		bufLog->WriteAccessLogToFile(outputLog, this->m_faultInjector.GetBitDepth(), this->m_dataSizeInBytes, bufferAccessedBytes, totalTargetInjections, padding);
	}

	outputLog << padding << "BUFFER TOTALS" << std::endl;

	for (size_t i = 0; i < AccessPrecision::Size; ++i) {
		for (size_t j = 0; j < AccessTypes::Size; ++j) {
			WriteAccessedBytesToFile(outputLog, this->m_faultInjector.GetBitDepth(), this->m_dataSizeInBytes, bufferAccessedBytes[i][j], AccessTypesNames[j], "Buffer " + AccessPrecisionNames[i], padding);
			totalTargetAccessesBytes[i][j] += bufferAccessedBytes[i][j];
		}
	}

	outputLog << padding << "Buffer Active Periods: " << activePeriodsCount << std::endl;

	outputLog << basePadding << "BUFFER END" << std::endl;
}

void ApproximateBuffer::WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& totalTargetEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const std::string& basePadding) const {
	const std::string padding = basePadding + '\t';
	
	outputLog << std::endl;
	this->WriteLogHeaderToFile(outputLog, basePadding);

	uint64_t activePeriodsCount	= 0;
	std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> bufferEnergy;
	std::fill_n(bufferEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, 0);

	for (const auto& [_, bufLog] : this->m_bufferLogs) {
		++activePeriodsCount;
		bufLog->WriteEnergyLogToFile(outputLog, bufferEnergy, respectiveConsumptionProfile, this->m_faultInjector.GetBitDepth(), this->m_dataSizeInBytes, this->GetSoftwareBufferSizeInBytes(), padding);
	}

	outputLog << padding << "BUFFER TOTALS" << std::endl;

	WriteEnergyConsumptionToLogFile(outputLog, bufferEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);
	//WriteEnergyConsumptionSavingsToLogFile(outputLog, bufferEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);
	AddEnergyConsumption(totalTargetEnergy, bufferEnergy);

	outputLog << padding << "Buffer Active Periods: " << activePeriodsCount << std::endl;

	outputLog << basePadding << "BUFFER END" << std::endl;
}

/* ==================================================================== */
/* Short Term Approximate Buffer										*/
/* ==================================================================== */

ShortTermApproximateBuffer::ShortTermApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
													const InjectionConfigurationReference& injectorCfg) : 
													ApproximateBuffer(bufferRange, id, creationPeriod, dataSizeInBytes, injectorCfg),
													m_pendingWrites(), m_remainingReads(), m_readHint(m_remainingReads.cend())
													{}

//WAS LOCKED (INDIRECTLY)
ShortTermApproximateBuffer::~ShortTermApproximateBuffer() {
	ShortTermApproximateBuffer::RetireBuffer(false);
	ApproximateBuffer::~ApproximateBuffer();
}

//WAS LOCKED
bool ShortTermApproximateBuffer::RetireBuffer(const bool giveAwayRecords) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	if (this->m_isActive >= 1) { //if there's at least one thread using it...
		this->m_isActive--;

		if (this->m_isActive == 0) { //failsafe against repeated retirements
			this->ReverseAllReadErrors();
			this->ApplyAllWriteErrors();
			#if ENABLE_PASSIVE_INJECTION
				this->ApplyAllPassiveErrors(); 
			#endif

			this->StoreCurrentPeriodLog();

			ApproximateBuffer::GiveAwayRecordsAndBackups(giveAwayRecords);

			return true;
		} else {
			return false;
		}
	} else {
		return true;
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//MUST LOCK
void ShortTermApproximateBuffer::BackupReadData(uint8_t* const data) {
	uint8_t * const readBackup = new uint8_t[this->m_minimumReadBackupSize];
	std::copy_n(data, this->m_minimumReadBackupSize, readBackup);
	this->m_remainingReads.insert(this->m_readHint, {data, readBackup});
}

//WAS LOCKED
void ShortTermApproximateBuffer::ReactivateBuffer(const uint64_t period) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	if (this->m_isActive == 0) {
		ApproximateBuffer::ReactivateBuffer(period);
	}

	this->m_isActive++;

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//MUST LOCK
uint8_t* ShortTermApproximateBuffer::GetWriteAddressFromIterator(const PendingWrites::const_iterator& it) {
	#if !MULTIPLE_BER_CONFIGURATION && !LOG_FAULTS
		return *it;
	#else
		return it->first;
	#endif
}

//MUST LOCK
auto ShortTermApproximateBuffer::GetWriteBerFromIterator(const PendingWrites::const_iterator& it) {
	#if MULTIPLE_BER_CONFIGURATION
		#if LOG_FAULTS
			return it->second.first;
		#else
			return it->second;
		#endif
	#else
		#if !DISTANCE_BASED_FAULT_INJECTOR
			return this->m_faultInjector.GetBer(ErrorCategory::Write);
		#else
			return this->m_faultInjector.GetInjectorRecord(ErrorCategory::Write);
		#endif
	#endif
}

#if LOG_FAULTS
	//MUST LOCK
	uint64_t* ShortTermApproximateBuffer::GetWriteErrorsLogFromIterator(const PendingWrites::const_iterator& it) {
		#if MULTIPLE_BER_CONFIGURATION
			return it->second.second;
		#else
			return it->second;
		#endif
	}
#endif

//MUST LOCK
PendingWrites::const_iterator ShortTermApproximateBuffer::ApplyFaultyWrite(const PendingWrites::const_iterator it) {
	uint8_t* const address = ShortTermApproximateBuffer::GetWriteAddressFromIterator(it);
	const auto ber = ShortTermApproximateBuffer::GetWriteBerFromIterator(it); 

	#if !DISTANCE_BASED_FAULT_INJECTOR
		this->m_faultInjector.InjectFault(address, ber, nullptr AND_LOG_ARGUMENT(ShortTermApproximateBuffer::GetWriteErrorsLogFromIterator(it)));
	#else
		this->m_faultInjector.InjectFault(address, *ber, static_cast<ssize_t>(this->m_dataSizeInBytes), nullptr AND_LOG_ARGUMENT(ShortTermApproximateBuffer::GetWriteErrorsLogFromIterator(it)));
	#endif

	return this->m_pendingWrites.erase(it);
}

//MUST LOCK
void ShortTermApproximateBuffer::ApplyFaultyWrite(uint8_t * const accessedAddress) {
	const PendingWrites::const_iterator it = this->m_pendingWrites.lower_bound(accessedAddress);
	if (it != this->m_pendingWrites.cend())	{
		this->ApplyFaultyWrite(it);
	}
}

//MUST LOCK
void ShortTermApproximateBuffer::ApplyFaultyWrite(uint8_t * const initialAddress, uint8_t const * const finalAddress) {
	PendingWrites::const_iterator lowerIt = this->m_pendingWrites.lower_bound(initialAddress);
	#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
		while (lowerIt != this->m_pendingWrites.cend() && lowerIt->first	< finalAddress)
	#else
		while (lowerIt != this->m_pendingWrites.cend() && *lowerIt			< finalAddress)
	#endif
	{
		lowerIt = this->ApplyFaultyWrite(lowerIt);
	}
}

//MUST LOCK
void ShortTermApproximateBuffer::ApplyAllWriteErrors() {
	for (PendingWrites::const_iterator it = this->m_pendingWrites.cbegin(); it != this->m_pendingWrites.cend(); /**/) {
		it = this->ApplyFaultyWrite(it);
	}
}

//MUST LOCK
void ShortTermApproximateBuffer::RecordFaultyWrite(uint8_t* const address, PendingWrites::const_iterator& hint) {
	#if MULTIPLE_BER_CONFIGURATION
		#if LOG_FAULTS
			#if !DISTANCE_BASED_FAULT_INJECTOR
				const auto& insertedValue = std::make_pair(this->m_faultInjector.GetBer(ErrorCategory::Write), this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Write));
			#else
				std::pair<DistanceBasedInjectorRecord*, uint64_t*> insertedValue = std::make_pair(this->m_faultInjector.GetInjectorRecord(ErrorCategory::Write), this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Write));
			#endif
		#else
			#if !DISTANCE_BASED_FAULT_INJECTOR
				const auto& insertedValue = this->m_faultInjector.GetBer(ErrorCategory::Write);
			#else
				DistanceBasedInjectorRecord* insertedValue = this->m_faultInjector.GetInjectorRecord(ErrorCategory::Write);
			#endif
		#endif

		hint = this->m_pendingWrites.insert_or_assign(hint, address, insertedValue);
	#else
		#if LOG_FAULTS
			hint = this->m_pendingWrites.insert_or_assign(hint, address, this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Write));
		#else
			hint = this->m_pendingWrites.insert(hint, address);
		#endif
	#endif

	++hint;
}

//MUST LOCK
RemainingReads::const_iterator ShortTermApproximateBuffer::ReverseFaultyRead(const RemainingReads::const_iterator it) {
	std::copy_n(it->second, this->m_minimumReadBackupSize, it->first);
	delete[] it->second;
	return this->m_remainingReads.erase(it);
}

//MUST LOCK
RemainingReads::const_iterator ShortTermApproximateBuffer::ReverseFaultyRead(uint8_t * const accessedAddress) {
	RemainingReads::const_iterator it = this->m_remainingReads.find(accessedAddress);
	if (it != this->m_remainingReads.cend()) {
		it = this->ReverseFaultyRead(it);
	}
	return it;
}

//MUST LOCK
RemainingReads::const_iterator ShortTermApproximateBuffer::ReverseFaultyRead(uint8_t * const initialAddress, uint8_t const * const finalAddress) {
	RemainingReads::const_iterator lowerIt = this->m_remainingReads.lower_bound(initialAddress); 
	while (lowerIt != this->m_remainingReads.cend() && lowerIt->first < finalAddress) {
		lowerIt = this->ReverseFaultyRead(lowerIt);
	}
	return lowerIt;
}

//MUST LOCK
void ShortTermApproximateBuffer::ReverseAllReadErrors() {
	for (RemainingReads::const_iterator it = this->m_remainingReads.cbegin(); it != this->m_remainingReads.cend(); /**/) {
		it = this->ReverseFaultyRead(it);
	}
}

//MUST LOCK
RemainingReads::const_iterator ShortTermApproximateBuffer::InvalidateRemainingRead(const RemainingReads::const_iterator it) {
	delete[] it->second;
	return this->m_remainingReads.erase(it);
}

//MUST LOCK
void ShortTermApproximateBuffer::InvalidateRemainingRead(uint8_t * const accessedAddress) {
	const RemainingReads::const_iterator it = this->m_remainingReads.find(accessedAddress); 
	if (it != this->m_remainingReads.cend()) {
		this->InvalidateRemainingRead(it);
	}
}

//MUST LOCK
void ShortTermApproximateBuffer::InvalidateRemainingRead(uint8_t * const initialAddress, uint8_t const * const finalAddress) {
	RemainingReads::const_iterator lowerIt = this->m_remainingReads.lower_bound(initialAddress); 
	while (lowerIt != this->m_remainingReads.cend() && lowerIt->first < finalAddress) {
		lowerIt = this->InvalidateRemainingRead(lowerIt);
	}
}

//WAS LOCKED
void ShortTermApproximateBuffer::HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	uint8_t const * const finalAddress = initialAddress + accessSize;

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)
	
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, accessSize);

	this->InvalidateRemainingRead(initialAddress, finalAddress);

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR 
		this->UpdateLastAccessPeriod(initialAddress);
	#endif
	
	if (this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread))) {
		PendingWrites::const_iterator hint = this->m_pendingWrites.lower_bound(initialAddress);
		for (uint8_t* currentAddress = initialAddress; currentAddress < finalAddress; currentAddress += this->m_dataSizeInBytes) {
			this->RecordFaultyWrite(currentAddress, hint);
		}
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED
void ShortTermApproximateBuffer::HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryWriteSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)
	this->HandleMemoryWriteSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED
void ShortTermApproximateBuffer::HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, this->m_dataSizeInBytes);

	this->InvalidateRemainingRead(accessedAddress);

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		this->UpdateLastAccessPeriod(accessedAddress);
	#endif

	if (this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread))) {
		PendingWrites::const_iterator hint = this->m_pendingWrites.lower_bound(accessedAddress);
		this->RecordFaultyWrite(accessedAddress, hint);
	}
}

//WAS LOCKED
void ShortTermApproximateBuffer::HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i); //it could also be implemented in something along the lines of SIMD version, but it'd also trigger pendings and remainings in between, also i'm lazy right now and don't even know why i still maintain this term approach
		this->HandleMemoryWriteSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED
void ShortTermApproximateBuffer::HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	uint8_t const * const finalAddress = initialAddress + accessSize;

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, accessSize);
	
	this->m_readHint = this->ReverseFaultyRead(initialAddress, finalAddress);

	this->ApplyFaultyWrite(initialAddress, finalAddress);

	#if !DISTANCE_BASED_FAULT_INJECTOR && ENABLE_PASSIVE_INJECTION
		this->ApplyPassiveFault(initialAddress, finalAddress);
	#endif

	if (this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread))) {		
		#if !DISTANCE_BASED_FAULT_INJECTOR
			for (uint8_t* currentAddress = initialAddress; currentAddress < finalAddress; currentAddress += this->m_dataSizeInBytes) {
				this->m_faultInjector.InjectFault(currentAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
			}
		#else
			this->m_faultInjector.InjectFault(initialAddress, ErrorCategory::Read, static_cast<ssize_t>(accessSize), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		#endif
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED
void ShortTermApproximateBuffer::HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryReadSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)
	this->HandleMemoryReadSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//MUST LOCK
void ShortTermApproximateBuffer::HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, this->m_dataSizeInBytes);

	this->m_readHint = this->ReverseFaultyRead(accessedAddress);

	this->ApplyFaultyWrite(accessedAddress);

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		this->ApplyPassiveFault(accessedAddress);
	#endif

	if (this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread))) {		
		#if !DISTANCE_BASED_FAULT_INJECTOR
			this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		#else
			this->m_faultInjector.InjectFault(accessedAddress, ErrorCategory::Read, static_cast<ssize_t>(this->m_dataSizeInBytes), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		#endif
	}
}

//WAS LOCKED
void ShortTermApproximateBuffer::HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i); //it could also be implemented in something along the lines of SIMD version, but it'd also trigger pendings and remainings in between, also i'm lazy right now and don't even know why i still maintain this term approach
		this->HandleMemoryReadSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

/* ==================================================================== */
/* Long Term Approximate Buffer											*/
/* ==================================================================== */

//WAS LOCKED
LongTermApproximateBuffer::LongTermApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
						  	const InjectionConfigurationReference& injectorCfg) : 
							ApproximateBuffer(bufferRange, id, creationPeriod, dataSizeInBytes, injectorCfg) {
	
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)
	this->InitializeRecordsAndBackups(creationPeriod);
	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED (INDIRECTLY)
LongTermApproximateBuffer::~LongTermApproximateBuffer() {
	LongTermApproximateBuffer::RetireBuffer(false);
	ApproximateBuffer::~ApproximateBuffer();
}

//MUST LOCK
void LongTermApproximateBuffer::InitializeRecordsAndBackups(const uint64_t period) {
	using namespace BorrowedMemory;

	const InjectionRecordPool::iterator recordIt = g_injectionRecords.find(this->GetNumberOfElements());
	if (recordIt != g_injectionRecords.cend()) {
		this->m_records = std::unique_ptr<InjectionRecord[]>(recordIt->second.release());
		g_injectionRecords.erase(recordIt);
	} else {
		this->m_records = std::make_unique<InjectionRecord[]>(this->GetNumberOfElements());
	}

	const ReadBackupsPool::iterator readIt = g_readBackups.find(this->GetTotalNecessaryReadBackupSize());
	if (readIt != g_readBackups.cend()) {
		this->m_readBackups = std::unique_ptr<uint8_t[]>(readIt->second.release());
		g_readBackups.erase(readIt);
	} else {
		this->m_readBackups = std::unique_ptr<uint8_t[]>((uint8_t*) std::malloc(this->GetTotalNecessaryReadBackupSize()));
	}

	#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
		const WriteSupportRecordPool::iterator writeIt = g_writeSupportRecordPool.find(this->GetNumberOfElements());
		if (writeIt != g_writeSupportRecordPool.cend()) {
			this->m_writeSupportRecords = std::unique_ptr<WriteSupportRecord[]>(writeIt->second.release());
			g_writeSupportRecordPool.erase(writeIt);
		} else {
			this->m_writeSupportRecords = std::unique_ptr<WriteSupportRecord[]>((WriteSupportRecord*) std::malloc(sizeof(WriteSupportRecord) * this->GetNumberOfElements()));
		}
	#endif
}


//MUST LOCK
void LongTermApproximateBuffer::GiveAwayRecordsAndBackups(const bool giveAwayRecords) {
	ApproximateBuffer::GiveAwayRecordsAndBackups(giveAwayRecords);

	if (giveAwayRecords) {
		BorrowedMemory::g_injectionRecords.insert({this->GetNumberOfElements(), std::unique_ptr<InjectionRecord[]>(this->m_records.release())});
		BorrowedMemory::g_readBackups.insert({this->GetTotalNecessaryReadBackupSize(), std::unique_ptr<uint8_t[]>(this->m_readBackups.release())});

		#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
			BorrowedMemory::g_writeSupportRecordPool.insert({this->GetNumberOfElements(), std::unique_ptr<WriteSupportRecord[]>(this->m_writeSupportRecords.release())});
		#endif

		#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
			BorrowedMemory::g_lastAccessPeriodPool.insert({this->GetNumberOfElements(), std::unique_ptr<uint64_t[]>(this->m_lastAccessPeriod.release())});
		#endif

	} else {
		this->m_records.reset();
		this->m_readBackups.reset();

		#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
			this->m_writeSupportRecords.reset();
		#endif

		#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
			this->m_lastAccessPeriod.reset();
		#endif
	}
}

//WAS LOCKED
bool LongTermApproximateBuffer::RetireBuffer(const bool giveAwayRecords) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	if (this->m_isActive >= 1) { //if there's at least one thread using it...
		this->m_isActive--;

		if (this->m_isActive == 0) { //failsafe against repeated retirements
			uint8_t* address = this->m_initialAddress;
			for (size_t elementIndex = 0; elementIndex < this->GetNumberOfElements(); ++elementIndex, address += this->m_dataSizeInBytes) {
				this->ProcessReadMemoryElement(elementIndex, address, false);
			}		

			#if ENABLE_PASSIVE_INJECTION && DISTANCE_BASED_FAULT_INJECTOR //otherwise, applied by the loop above
				this->ApplyAllPassiveErrors(); 
			#endif

			this->StoreCurrentPeriodLog();

			this->GiveAwayRecordsAndBackups(giveAwayRecords);
			
			return true;
		} else {
			return false;
		}
	} else {
		return true;
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED
void LongTermApproximateBuffer::ReactivateBuffer(const uint64_t period) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	if (this->m_isActive == 0) { //failsafe againt repeated reactivations
		ApproximateBuffer::ReactivateBuffer(period);
		LongTermApproximateBuffer::InitializeRecordsAndBackups(period);
	}

	this->m_isActive++;

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//MUST LOCK
void LongTermApproximateBuffer::RecordFaultyWrite(const size_t elementIndex) {
	#if MULTIPLE_BER_CONFIGURATION
		#if !DISTANCE_BASED_FAULT_INJECTOR
			this->m_writeSupportRecords[elementIndex].writeSupport = this->m_faultInjector.GetBer(ErrorCategory::Write);
		#else
			this->m_writeSupportRecords[elementIndex].writeSupport = this->m_faultInjector.GetInjectorRecord(ErrorCategory::Write);
		#endif
	#endif

	#if LOG_FAULTS
		this->m_writeSupportRecords[elementIndex].writeErrorsCountByBit	= this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Write);
	#endif
}

uint8_t* LongTermApproximateBuffer::GetBackupAddressFromIndex(const size_t index) const {
	return &(this->m_readBackups[index * this->m_minimumReadBackupSize]);
}

//MUST LOCK
void LongTermApproximateBuffer::ReverseFaultyRead(const size_t elementIndex, uint8_t* const accessedAddress) {
	std::copy_n(this->GetBackupAddressFromIndex(elementIndex), this->m_minimumReadBackupSize, accessedAddress);
}

//MUST LOCK
auto LongTermApproximateBuffer::GetWriteBer(const size_t elementIndex) {
	#if !DISTANCE_BASED_FAULT_INJECTOR
		#if MULTIPLE_BER_CONFIGURATION 
			return this->m_writeSupportRecords[elementIndex].writeSupport;
		#else
			return this->m_faultInjector.GetBer(ErrorCategory::Write);
		#endif
	#else
		#if MULTIPLE_BER_CONFIGURATION
			return this->m_writeSupportRecords[elementIndex].writeSupport;
		#else
			return this->m_faultInjector.GetInjectorRecord(ErrorCategory::Write);
		#endif
	#endif
}

//MUST LOCK
void LongTermApproximateBuffer::ApplyWriteFault(const size_t elementIndex, uint8_t* const accessedAddress) {
	auto ber = this->GetWriteBer(elementIndex);

	#if OVERCHARGE_BER 
		ber += this->m_faultInjector.GetBer(ErrorCategory::Passive, this->m_lastAccessPeriod[elementIndex], this->GetCurrentPassiveBerMarker());
		this->m_lastAccessPeriod[elementIndex] = this->GetCurrentPassiveBerMarker();

		#if OVERCHARGE_FLIP_BACK
			this->m_faultInjector.InjectFaultOvercharged(accessedAddress, ber);
		#else
			this->m_faultInjector.InjectFault(accessedAddress, ber, nullptr);
		#endif
	#else
		#if !DISTANCE_BASED_FAULT_INJECTOR
			this->m_faultInjector.InjectFault(accessedAddress, ber, nullptr AND_LOG_ARGUMENT(this->m_writeSupportRecords[elementIndex].writeErrorsCountByBit));
		#else
			//USING THE DISTANCE_BASED_FAULT_INJECTOR THE ERRORS ARE INSERTED EVERY NEXTPERIOD() OR RETIREBUFFER()
			this->m_faultInjector.InjectFault(accessedAddress, *ber, static_cast<ssize_t>(this->m_dataSizeInBytes), nullptr AND_LOG_ARGUMENT(this->m_writeSupportRecords[elementIndex].writeErrorsCountByBit));
		#endif
	#endif
}

//MUST LOCK
void LongTermApproximateBuffer::BackupReadData(uint8_t* const data) {
	const size_t elementIndex = this->GetIndexFromAddress(data);
	uint8_t* const backupAddress = this->GetBackupAddressFromIndex(elementIndex);
	std::copy_n(data, this->m_minimumReadBackupSize, backupAddress);
	this->m_records[elementIndex].errorStatus = ErrorStatus::Read;
}

//MUST LOCK
void LongTermApproximateBuffer::ProcessWrittenMemoryElement(const size_t elementIndex, const uint8_t newStatus, const bool shouldInject) {
	this->m_records[elementIndex].errorStatus = newStatus;

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		this->UpdateLastAccessPeriod(elementIndex);
	#endif

	#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
		if (shouldInject) {
			this->RecordFaultyWrite(elementIndex);
		}
	#endif
}

//MUST LOCK
void LongTermApproximateBuffer::ProcessReadMemoryElement(const size_t elementIndex, uint8_t* const accessedAddress, const bool shouldInject) {
	uint8_t& currentErrorStatus = this->m_records[elementIndex].errorStatus;

	if (currentErrorStatus != ErrorStatus::None) {
		if (currentErrorStatus == ErrorStatus::Read) {
			this->ReverseFaultyRead(elementIndex, accessedAddress);
		} else {
			this->ApplyWriteFault(elementIndex, accessedAddress);
		}
		currentErrorStatus = ErrorStatus::None;
	}

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		this->ApplyPassiveFault(elementIndex, accessedAddress);
	#endif

	#if !DISTANCE_BASED_FAULT_INJECTOR //outside of the function to avoid constant rechecking during SIMD or Scattered, must be added
		if (shouldInject) {
			this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		}
	#endif
}

//WAS LOCKED
void LongTermApproximateBuffer::HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	const size_t firstElementIndex = this->GetIndexFromAddress(initialAddress);
	const size_t accessedElementCount = accessSize / this->m_dataSizeInBytes;
	const size_t endElementIndex = firstElementIndex + accessedElementCount;

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, accessSize);

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	const uint8_t newStatus = (shouldInject ? ErrorStatus::Write : ErrorStatus::None);

	for (size_t elementIndex = firstElementIndex; elementIndex < endElementIndex; ++elementIndex) {
		this->ProcessWrittenMemoryElement(elementIndex, newStatus, shouldInject);
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}


//WAS LOCKED
void LongTermApproximateBuffer::HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryWriteSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)
	this->HandleMemoryWriteSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

void LongTermApproximateBuffer::HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, this->m_dataSizeInBytes);

	const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);
	const bool shouldInject = this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	const uint8_t newStatus = (shouldInject ? ErrorStatus::Write : ErrorStatus::None);

	this->ProcessWrittenMemoryElement(elementIndex, newStatus, shouldInject);
}

//WAS LOCKED
void LongTermApproximateBuffer::HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, this->m_dataSizeInBytes * memOpInfo->NumOfElements());

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	const uint8_t newStatus = (shouldInject ? ErrorStatus::Write : ErrorStatus::None);

	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i);
		const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);

		this->ProcessWrittenMemoryElement(elementIndex, newStatus, shouldInject);
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED
void LongTermApproximateBuffer::HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	const size_t firstElementIndex = this->GetIndexFromAddress(initialAddress);
	uint8_t* currentAddress = initialAddress;
	uint8_t const * const finalAddress = initialAddress + accessSize;

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, accessSize);

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread)); 

	for (size_t currentElementIndex = firstElementIndex; currentAddress < finalAddress; ++currentElementIndex, currentAddress += this->m_dataSizeInBytes) {
		this->ProcessReadMemoryElement(currentElementIndex, currentAddress, shouldInject);
	}

	#if DISTANCE_BASED_FAULT_INJECTOR //outside of the loop to avoid constant rechecking
		if (shouldInject) {
			this->m_faultInjector.InjectFault(initialAddress, ErrorCategory::Read, static_cast<ssize_t>(accessSize), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		}
	#endif

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

//WAS LOCKED
void LongTermApproximateBuffer::HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryReadSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)
	this->HandleMemoryReadSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}

void LongTermApproximateBuffer::HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, this->m_dataSizeInBytes);

	const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);
	const bool shouldInject = this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));

	this->ProcessReadMemoryElement(elementIndex, accessedAddress, shouldInject);

	#if DISTANCE_BASED_FAULT_INJECTOR
		if (shouldInject) {
			this->m_faultInjector.InjectFault(accessedAddress, ErrorCategory::Read, static_cast<ssize_t>(this->m_dataSizeInBytes), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		}
	#endif
}

//WAS LOCKED
void LongTermApproximateBuffer::HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	//IF_PIN_PRIVATE_LOCKED(PIN_GetLock(&this->m_bufferLock, -1);)

	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, this->m_dataSizeInBytes * memOpInfo->NumOfElements());

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));

	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i);
		const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);

		this->ProcessReadMemoryElement(elementIndex, accessedAddress, shouldInject);

		#if DISTANCE_BASED_FAULT_INJECTOR //has to be here due to non-contiguos access
			if (shouldInject) {
				this->m_faultInjector.InjectFault(accessedAddress, ErrorCategory::Read, static_cast<ssize_t>(this->m_dataSizeInBytes), this AND_LOG_ARGUMENT(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
			}
		#endif
	}

	//IF_PIN_PRIVATE_LOCKED(PIN_ReleaseLock(&this->m_bufferLock);)
}