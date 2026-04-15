/* ==================================================================== */
/* Short Term Approximate Buffer										*/
/* ==================================================================== */

#include "approximate-buffer-map-record.h"

ApproximateBufferMapRecord::ApproximateBufferMapRecord(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
													const InjectionConfigurationReference& injectorCfg) : 
													ApproximateBuffer(bufferRange, id, creationPeriod, dataSizeInBytes, injectorCfg),
													m_pendingWrites(), m_remainingReads(), m_readHint(IF_LSBDROPPED_ELSE(m_remainingReads.begin(), m_remainingReads.cbegin()))
													{}

//WAS LOCKED (INDIRECTLY)
ApproximateBufferMapRecord::~ApproximateBufferMapRecord() {
	ApproximateBufferMapRecord::RetireBuffer(false);
	ApproximateBuffer::~ApproximateBuffer();
}

//WAS LOCKED
bool ApproximateBufferMapRecord::RetireBuffer(const bool giveAwayRecords) {
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
}

//MUST LOCK
void ApproximateBufferMapRecord::BackupReadData(uint8_t* const data IF_COMMA_LSBDROPPED(const bool isLSBDrop/*= false*/)) {
	#if LSB_DROPPING
		uint8_t * const readBackup = new uint8_t[this->m_minimumReadBackupSize];
		std::copy_n(data, this->m_minimumReadBackupSize, readBackup);
		auto entry = std::make_pair(isLSBDrop, std::move(std::unique_ptr<uint8_t[]>(readBackup)));
		this->m_readHint = this->m_remainingReads.insert(this->m_readHint, {data, std::move(entry)});
	#else
		uint8_t * const readBackup = new uint8_t[this->m_minimumReadBackupSize];
		std::copy_n(data, this->m_minimumReadBackupSize, readBackup);
		this->m_readHint = this->m_remainingReads.insert(this->m_readHint, {data, readBackup});
	#endif
}

//WAS LOCKED
void ApproximateBufferMapRecord::ReactivateBuffer(const int64_t period) {
	if (this->m_isActive == 0) {
		ApproximateBuffer::ReactivateBuffer(period);
	}

	this->m_isActive++;
}

//MUST LOCK
uint8_t* ApproximateBufferMapRecord::GetWriteAddressFromIterator(const PendingWrites::const_iterator& it) {
	#if !MULTIPLE_BER_CONFIGURATION && !LOG_FAULTS
		return *it;
	#else
		return it->first;
	#endif
}

//MUST LOCK
auto ApproximateBufferMapRecord::GetWriteBerFromIterator(const PendingWrites::const_iterator& it) {
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
	uint64_t* ApproximateBufferMapRecord::GetWriteErrorsLogFromIterator(const PendingWrites::const_iterator& it) {
		#if MULTIPLE_BER_CONFIGURATION
			return it->second.second;
		#else
			return it->second;
		#endif
	}
#endif

//MUST LOCK
PendingWrites::const_iterator ApproximateBufferMapRecord::ApplyFaultyWrite(const PendingWrites::const_iterator it) {
	uint8_t* const address = ApproximateBufferMapRecord::GetWriteAddressFromIterator(it);
	const auto ber = ApproximateBufferMapRecord::GetWriteBerFromIterator(it); 

	#if !DISTANCE_BASED_FAULT_INJECTOR
		this->m_faultInjector.InjectFault(address, ber, nullptr IF_COMMA_LOGGING_FAULTS(ApproximateBufferMapRecord::GetWriteErrorsLogFromIterator(it)));
	#else
		this->m_faultInjector.InjectFault(address, *ber, static_cast<ssize_t>(this->m_dataSizeInBytes), nullptr IF_COMMA_LOGGING_FAULTS(ApproximateBufferMapRecord::GetWriteErrorsLogFromIterator(it)));
	#endif

	return this->m_pendingWrites.erase(it);
}

//MUST LOCK
void ApproximateBufferMapRecord::ApplyFaultyWrite(uint8_t * const accessedAddress) {
	const PendingWrites::const_iterator it = this->m_pendingWrites.lower_bound(accessedAddress);
	if (it != this->m_pendingWrites.cend())	{
		this->ApplyFaultyWrite(it);
	}
}

//MUST LOCK
void ApproximateBufferMapRecord::ApplyFaultyWrite(uint8_t * const initialAddress, uint8_t const * const finalAddress) {
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
void ApproximateBufferMapRecord::ApplyAllWriteErrors() {
	for (PendingWrites::const_iterator it = this->m_pendingWrites.cbegin(); it != this->m_pendingWrites.cend(); /**/) {
		it = this->ApplyFaultyWrite(it);
	}
}

//MUST LOCK
void ApproximateBufferMapRecord::RecordFaultyWrite(uint8_t* const address, PendingWrites::const_iterator& hint) {
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
RemainingReadsIterator ApproximateBufferMapRecord::ReverseFaultyRead(const RemainingReadsIterator it IF_COMMA_LSBDROPPED(const bool reverseLSBDrop/*= true*/)) {
	#if LSB_DROPPING
		if (it->second.first && !reverseLSBDrop) { //if just LSBDropping...
			return std::next(it);
		} else {
			std::copy_n(it->second.second.get(), this->m_minimumReadBackupSize, it->first);
			//delete[] it->second.second; //unique_ptr now, no need to delete
			return this->m_remainingReads.erase(it);
		}
	#else
		std::copy_n(it->second, this->m_minimumReadBackupSize, it->first);
		delete[] it->second;
		return this->m_remainingReads.erase(it);
	#endif
}


//MUST LOCK
RemainingReadsIterator ApproximateBufferMapRecord::ReverseFaultyRead(uint8_t * const accessedAddress IF_COMMA_LSBDROPPED(const bool reverseLSBDrop/*= true*/)) {
	RemainingReadsIterator it = this->m_remainingReads.find(accessedAddress);
	if (it != this->m_remainingReads.cend()) {
		it = this->ReverseFaultyRead(it IF_COMMA_LSBDROPPED(reverseLSBDrop));
	}
	return it;
}

//MUST LOCK
RemainingReadsIterator ApproximateBufferMapRecord::ReverseFaultyRead(uint8_t * const initialAddress, uint8_t const * const finalAddress IF_COMMA_LSBDROPPED(const bool reverseLSBDrop/*= true*/)) {
	RemainingReadsIterator lowerIt = this->m_remainingReads.lower_bound(initialAddress); 
	while (lowerIt != this->m_remainingReads.cend() && lowerIt->first < finalAddress) {
		lowerIt = this->ReverseFaultyRead(lowerIt IF_COMMA_LSBDROPPED(reverseLSBDrop));
	}
	return lowerIt;
}

//MUST LOCK
void ApproximateBufferMapRecord::ReverseAllReadErrors() {
	for (RemainingReadsIterator it = IF_LSBDROPPED_ELSE(this->m_remainingReads.begin(), this->m_remainingReads.cbegin()); it != this->m_remainingReads.cend(); /**/) {
		it = this->ReverseFaultyRead(it); //reverseLSBDrop = true
	}
}

//MUST LOCK
RemainingReads::const_iterator ApproximateBufferMapRecord::InvalidateRemainingRead(const RemainingReads::const_iterator it) {
	#if LSB_DROPPING
		//delete[] it->second.second;
	#else
		delete[] it->second;
	#endif
	return this->m_remainingReads.erase(it);
}

//MUST LOCK
void ApproximateBufferMapRecord::InvalidateRemainingRead(uint8_t * const accessedAddress) {
	const RemainingReads::const_iterator it = this->m_remainingReads.find(accessedAddress); 
	if (it != this->m_remainingReads.cend()) {
		this->InvalidateRemainingRead(it);
	}
}

//MUST LOCK
void ApproximateBufferMapRecord::InvalidateRemainingRead(uint8_t * const initialAddress, uint8_t const * const finalAddress) {
	RemainingReads::const_iterator lowerIt = this->m_remainingReads.lower_bound(initialAddress); 
	while (lowerIt != this->m_remainingReads.cend() && lowerIt->first < finalAddress) {
		lowerIt = this->InvalidateRemainingRead(lowerIt);
	}
}

//WAS LOCKED
void ApproximateBufferMapRecord::HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, accessSize);

	uint8_t const * const finalAddress = initialAddress + accessSize;

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
}

//WAS LOCKED
void ApproximateBufferMapRecord::HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryWriteSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}

	
	this->HandleMemoryWriteSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
}

//WAS LOCKED
void ApproximateBufferMapRecord::HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
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
void ApproximateBufferMapRecord::HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i); //it could also be implemented in something along the lines of SIMD version, but it'd also trigger pendings and remainings in between, also i'm lazy right now and don't even know why i still maintain this term approach
		this->HandleMemoryWriteSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	}
}

#if LSB_DROPPING
	bool ApproximateBufferMapRecord::IsBackedUp(uint8_t const * const targetAddress) {
		while (this->m_readHint != this->m_remainingReads.cend() && this->m_readHint->first < targetAddress) {
			this->m_readHint++;
		}
		
		return this->m_readHint != this->m_remainingReads.cend() && this->m_readHint->first == targetAddress;
	}
#endif

//WAS LOCKED
void ApproximateBufferMapRecord::HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, accessSize);
	
	uint8_t const * const finalAddress = initialAddress + accessSize;
	
	this->m_readHint = this->ReverseFaultyRead(initialAddress, finalAddress IF_COMMA_LSBDROPPED(false));

	this->ApplyFaultyWrite(initialAddress, finalAddress);

	#if !DISTANCE_BASED_FAULT_INJECTOR && ENABLE_PASSIVE_INJECTION
		this->ApplyPassiveFault(initialAddress, finalAddress);
	#endif

	if (this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread))) {	
		#if !DISTANCE_BASED_FAULT_INJECTOR
			#if LSB_DROPPING
			 	if (this->m_faultInjector.HasLSBDropping()) {
					this->m_readHint = this->m_remainingReads.lower_bound(initialAddress); //maybe faster than just decreasing the readHint

					for (uint8_t* currentAddress = initialAddress; currentAddress < finalAddress; currentAddress += this->m_dataSizeInBytes) {
						const bool isBackedUp = this->IsBackedUp(currentAddress);
						
						//updated in InjectFault -> BackupReadData
						this->m_readHint->second.first = !this->m_faultInjector.InjectFault(currentAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), (!isBackedUp ? this : nullptr) IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
					}
			 	} else {
					if (this->m_readHint != this->m_remainingReads.cbegin()) {
						this->m_readHint--;
					}

					for (uint8_t* currentAddress = initialAddress; currentAddress < finalAddress; currentAddress += this->m_dataSizeInBytes) {
						this->m_faultInjector.InjectFault(currentAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
					}
				}
			#else
				if (this->m_readHint != this->m_remainingReads.cbegin()) {
					this->m_readHint--;
				}

				for (uint8_t* currentAddress = initialAddress; currentAddress < finalAddress; currentAddress += this->m_dataSizeInBytes) {
					this->m_faultInjector.InjectFault(currentAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
				}
			#endif
		#else
			if (this->m_readHint != this->m_remainingReads.cbegin()) {	
				this->m_readHint--;
			}

			this->m_faultInjector.InjectFault(initialAddress, ErrorCategory::Read, static_cast<ssize_t>(accessSize), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		#endif
	}
}

//WAS LOCKED
void ApproximateBufferMapRecord::HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	if (this->IsIgnorableMisaligned(accessedAddress, accessSize)) {
		return;
	}

	if (accessSize > this->m_dataSizeInBytes) {
		this->HandleMemoryReadSIMD(accessedAddress, accessSize, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
		return;
	}

	this->HandleMemoryReadSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
}

//MUST LOCK
void ApproximateBufferMapRecord::HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, this->m_dataSizeInBytes);

	this->m_readHint = this->ReverseFaultyRead(accessedAddress IF_COMMA_LSBDROPPED(false));

	this->ApplyFaultyWrite(accessedAddress);

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		this->ApplyPassiveFault(accessedAddress);
	#endif

	if (this->GetShouldInject(ErrorCategory::Read, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread))) {
		if (this->m_readHint != this->m_remainingReads.cbegin()) {	
			this->m_readHint--;
		}	
		#if !DISTANCE_BASED_FAULT_INJECTOR
			#if LSB_DROPPING
				if (this->m_faultInjector.HasLSBDropping()) {
					const bool isBackedUp = this->IsBackedUp(accessedAddress);
					
					//updated in InjectFault -> BackupReadData
					this->m_readHint->second.first = !this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), (!isBackedUp ? this : nullptr) IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
				} else {
					this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
				}
			#else
				this->m_faultInjector.InjectFault(accessedAddress, this->m_faultInjector.GetBer(ErrorCategory::Read), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
			#endif
		#else
			this->m_faultInjector.InjectFault(accessedAddress, ErrorCategory::Read, static_cast<ssize_t>(this->m_dataSizeInBytes), this IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Read)));
		#endif
	}
}

//WAS LOCKED
void ApproximateBufferMapRecord::HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
		uint8_t * const accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i); //it could also be implemented in something along the lines of SIMD version, but it'd also trigger pendings and remainings in between, also i'm lazy right now and don't even know why i still maintain this term approach
		this->HandleMemoryReadSingleElementUnsafe(accessedAddress, isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(isBufferInThread));
	}
}