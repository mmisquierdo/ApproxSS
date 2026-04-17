/* ==================================================================== */
/* Long Term Approximate Buffer											*/
/* ==================================================================== */

#include "approximate-buffer-array-record.h"

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
ApproximateBufferArrayRecord::ApproximateBufferArrayRecord(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
						  	const InjectionConfigurationReference& injectorCfg) : 
							ApproximateBuffer(bufferRange, id, creationPeriod, dataSizeInBytes, injectorCfg) {
	
	
	this->InitializeRecordsAndBackups(creationPeriod);
	
}

//WAS LOCKED (INDIRECTLY)
ApproximateBufferArrayRecord::~ApproximateBufferArrayRecord() {
	ApproximateBufferArrayRecord::RetireBuffer(false);
	ApproximateBuffer::~ApproximateBuffer();
}

//MUST LOCK
void ApproximateBufferArrayRecord::InitializeRecordsAndBackups(const uint64_t period) {
	this->m_lowestInjectedElement = this->GetIndexFromAddress(this->m_finalAddress); // should generate a valid (non-crashing) element
	this->m_highestInjectedElement = this->GetIndexFromAddress(this->m_initialAddress);

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
		//this->m_readBackups = std::unique_ptr<uint8_t[]>((uint8_t*) std::malloc(this->GetTotalNecessaryReadBackupSize())); //"not supported" by Pin 4.0
		//this->m_readBackups = std::make_unique_for_overwrite<uint8_t[]>(this->GetTotalNecessaryReadBackupSize()); //requires C++20, stuck in C++17 by Pin 4.0

		this->m_readBackups = std::unique_ptr<uint8_t[]>(new uint8_t[this->GetTotalNecessaryReadBackupSize()]);
	}

	#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
		const WriteSupportRecordPool::iterator writeIt = g_writeSupportRecordPool.find(this->GetNumberOfElements());
		if (writeIt != g_writeSupportRecordPool.cend()) {
			this->m_writeSupportRecords = std::unique_ptr<WriteSupportRecord[]>(writeIt->second.release());
			g_writeSupportRecordPool.erase(writeIt);
		} else {
			//this->m_writeSupportRecords = std::unique_ptr<WriteSupportRecord[]>((WriteSupportRecord*) std::malloc(sizeof(WriteSupportRecord) * this->GetNumberOfElements())); // "not supported" in Pin 4.0
			this->m_writeSupportRecords = std::unique_ptr<WriteSupportRecord[]>(new WriteSupportRecord[this->GetNumberOfElements()]);
		}
	#endif
}


//MUST LOCK
void ApproximateBufferArrayRecord::GiveAwayRecordsAndBackups(const bool giveAwayRecords) {
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
bool ApproximateBufferArrayRecord::RetireBuffer(const bool giveAwayRecords) {
	

	if (this->m_isActive >= 1) { //if there's at least one thread using it...
		this->m_isActive--;

		if (this->m_isActive == 0) { //failsafe against repeated retirements
			uint8_t* address = this->GetAddressFromIndex(this->m_lowestInjectedElement);
			for (size_t elementIndex = this->m_lowestInjectedElement; elementIndex <= this->m_highestInjectedElement; ++elementIndex, address += this->m_dataSizeInBytes) {
				this->ProcessReadMemoryElement(elementIndex, address, false); //reverseLSBDrop = true
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

	
}

//WAS LOCKED
void ApproximateBufferArrayRecord::ReactivateBuffer(const int64_t period) {
	if (this->m_isActive == 0) { //failsafe againt repeated reactivations
		ApproximateBuffer::ReactivateBuffer(period);
		ApproximateBufferArrayRecord::InitializeRecordsAndBackups(period);
	}

	this->m_isActive++;	
}

//MUST LOCK
void ApproximateBufferArrayRecord::RecordFaultyWriteSupport(const size_t elementIndex) {
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

uint8_t* ApproximateBufferArrayRecord::GetBackupAddressFromIndex(const size_t index) const {
	return &(this->m_readBackups[index * this->m_minimumReadBackupSize]);
}

//MUST LOCK, remember to change error status later
void ApproximateBufferArrayRecord::ReverseFaultyRead(const size_t elementIndex, uint8_t* const accessedAddress) {
	// LSBDropped: status chacked beforehand 
	std::copy_n(this->GetBackupAddressFromIndex(elementIndex), this->m_minimumReadBackupSize, accessedAddress);
}

//MUST LOCK
auto ApproximateBufferArrayRecord::GetWriteBer(const size_t elementIndex) {
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
void ApproximateBufferArrayRecord::ApplyWriteFault(const size_t elementIndex, uint8_t* const accessedAddress) {
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
			this->m_faultInjector.InjectFault(accessedAddress, ber, nullptr IF_COMMA_LOGGING_FAULTS(this->m_writeSupportRecords[elementIndex].writeErrorsCountByBit));
		#else
			//USING THE DISTANCE_BASED_FAULT_INJECTOR THE ERRORS ARE INSERTED EVERY NEXTPERIOD() OR RETIREBUFFER()
			this->m_faultInjector.InjectFault(accessedAddress, *ber, static_cast<ssize_t>(this->m_dataSizeInBytes), nullptr IF_COMMA_LOGGING_FAULTS(this->m_writeSupportRecords[elementIndex].writeErrorsCountByBit));
		#endif
	#endif
}

//MUST LOCK
void ApproximateBufferArrayRecord::BackupReadData(uint8_t* const data IF_COMMA_LSBDROPPED(const bool isLSBDrop/*= false*/)) {
	const size_t elementIndex = this->GetIndexFromAddress(data);
	uint8_t* const backupAddress = this->GetBackupAddressFromIndex(elementIndex);
	std::copy_n(data, this->m_minimumReadBackupSize, backupAddress);

	this->m_lowestInjectedElement = std::min(this->m_lowestInjectedElement, elementIndex);
	this->m_highestInjectedElement = std::max(this->m_highestInjectedElement, elementIndex);

	#if LSB_DROPPING
		this->m_records[elementIndex].errorStatus = isLSBDrop ? ErrorStatus::LSBDrop : ErrorStatus::Read;
	#else
		this->m_records[elementIndex].errorStatus = ErrorStatus::Read;
	#endif
}

//MUST LOCK, UPDATE LOWEST AND HIGHEST INJECTED ELEMENTS
void ApproximateBufferArrayRecord::ProcessWrittenMemoryElement(const size_t elementIndex, const uint8_t newStatus, const bool shouldInject) {
	this->m_records[elementIndex].errorStatus = newStatus;

	// DO THIS OUTSIDE THE LOOP
	//this->m_lowestInjectedElement = std::min(this->m_lowestInjectedElement, elementIndex);
	//this->m_highestInjectedElement = std::max(this->m_highestInjectedElement, elementIndex);

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		this->UpdateLastAccessPeriod(elementIndex);
	#endif

	#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
		if (shouldInject) {
			this->RecordFaultyWriteSupport(elementIndex);
		}
	#endif
}

//MUST LOCK
void ApproximateBufferArrayRecord::ProcessReadMemoryElement(const size_t elementIndex, uint8_t* const accessedAddress, const bool shouldInject IF_COMMA_LSBDROPPED(const bool reverseLSBDrop/*=true*/)) {
	uint8_t& currentErrorStatus = this->m_records[elementIndex].errorStatus;

	switch (currentErrorStatus)	{
		case ErrorStatus::Read:
			this->ReverseFaultyRead(elementIndex, accessedAddress);
			currentErrorStatus = ErrorStatus::None;
			break;
		case ErrorStatus::Write:
			this->ApplyWriteFault(elementIndex, accessedAddress);
			currentErrorStatus = ErrorStatus::None;
			break;
		#if LSB_DROPPING
		case ErrorStatus::LSBDrop:
			if (reverseLSBDrop) {
				this->ReverseFaultyRead(elementIndex, accessedAddress);
				currentErrorStatus = ErrorStatus::None;
			}
			break;
		#endif
		default:
			break;
	}

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		this->ApplyPassiveFault(elementIndex, accessedAddress);
	#endif

	#if !DISTANCE_BASED_FAULT_INJECTOR //outside of the function to avoid constant rechecking during SIMD or Scattered, must be added
		if (shouldInject) {
			#if LSB_DROPPING
				if (this->m_faultInjector.HasLSBDropping()) {
					const bool isBackedUp = currentErrorStatus == ErrorStatus::LSBDrop;

					this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), (!isBackedUp ? this : nullptr) IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read))); 
				} else {
					this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
				}
			#else
				this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
			#endif
		}
	#endif
}

//WAS LOCKED
void ApproximateBufferArrayRecord::HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, accessSize);
	
	const size_t firstElementIndex = this->GetIndexFromAddress(initialAddress);
	const size_t accessedElementCount = accessSize / this->m_dataSizeInBytes;
	const size_t endElementIndex = firstElementIndex + accessedElementCount;

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	const uint8_t newStatus = (shouldInject ? ErrorStatus::Write : ErrorStatus::None);

	if (shouldInject) {
		this->m_lowestInjectedElement = std::min(this->m_lowestInjectedElement, firstElementIndex);
		this->m_highestInjectedElement = std::max(this->m_highestInjectedElement, endElementIndex-1);
	}

	for (size_t elementIndex = firstElementIndex; elementIndex < endElementIndex; ++elementIndex) {
		this->ProcessWrittenMemoryElement(elementIndex, newStatus, shouldInject);
	}	
}

//WAS LOCKED
void ApproximateBufferArrayRecord::HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryWriteSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}
	
	this->HandleMemoryWriteSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
}

void ApproximateBufferArrayRecord::HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, this->m_dataSizeInBytes);

	const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);
	const bool shouldInject = this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	const uint8_t newStatus = (shouldInject ? ErrorStatus::Write : ErrorStatus::None);

	if (shouldInject) {
		this->m_lowestInjectedElement = std::min(this->m_lowestInjectedElement, elementIndex);
		this->m_highestInjectedElement = std::max(this->m_highestInjectedElement, elementIndex);
	}

	this->ProcessWrittenMemoryElement(elementIndex, newStatus, shouldInject);
}

//WAS LOCKED
void ApproximateBufferArrayRecord::HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, this->m_dataSizeInBytes * memOpInfo->NumOfElements());

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Write, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	const uint8_t newStatus = (shouldInject ? ErrorStatus::Write : ErrorStatus::None);
	
	if (shouldInject) {
		this->m_lowestInjectedElement = std::min(this->m_lowestInjectedElement, this->GetIndexFromAddress((uint8_t*) memOpInfo->ElementAddress(0)));
		this->m_highestInjectedElement = std::max(this->m_highestInjectedElement, this->GetIndexFromAddress((uint8_t*) memOpInfo->ElementAddress(memOpInfo->NumOfElements()-1))); // assuming they're ordered
	}

	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i);
		const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);

		this->ProcessWrittenMemoryElement(elementIndex, newStatus, shouldInject);
	}
}

//WAS LOCKED
void ApproximateBufferArrayRecord::HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, accessSize);
	
	const size_t firstElementIndex = this->GetIndexFromAddress(initialAddress);
	uint8_t* currentAddress = initialAddress;
	uint8_t const * const finalAddress = initialAddress + accessSize;

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread)); 

	for (size_t currentElementIndex = firstElementIndex; currentAddress < finalAddress; ++currentElementIndex, currentAddress += this->m_dataSizeInBytes) {
		this->ProcessReadMemoryElement(currentElementIndex, currentAddress, shouldInject);
	}

	#if DISTANCE_BASED_FAULT_INJECTOR //outside of the loop to avoid constant rechecking
		if (shouldInject) {
			this->m_faultInjector.InjectFault(initialAddress, ErrorCategory::Read, static_cast<ssize_t>(accessSize), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		}
	#endif
}

//WAS LOCKED
void ApproximateBufferArrayRecord::HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryReadSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}

	this->HandleMemoryReadSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
}

void ApproximateBufferArrayRecord::HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, this->m_dataSizeInBytes);

	const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);
	const bool shouldInject = this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));

	this->ProcessReadMemoryElement(elementIndex, accessedAddress, shouldInject);

	#if DISTANCE_BASED_FAULT_INJECTOR
		if (shouldInject) {
			this->m_faultInjector.InjectFault(accessedAddress, ErrorCategory::Read, static_cast<ssize_t>(this->m_dataSizeInBytes), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		}
	#endif
}

//WAS LOCKED
void ApproximateBufferArrayRecord::HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, this->m_dataSizeInBytes * memOpInfo->NumOfElements());

	const bool shouldInject = this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));

	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i);
		const size_t elementIndex = this->GetIndexFromAddress(accessedAddress);

		this->ProcessReadMemoryElement(elementIndex, accessedAddress, shouldInject);

		#if DISTANCE_BASED_FAULT_INJECTOR //has to be here due to non-contiguos access
			if (shouldInject) {
				this->m_faultInjector.InjectFault(accessedAddress, ErrorCategory::Read, static_cast<ssize_t>(this->m_dataSizeInBytes), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
			}
		#endif
	}
}