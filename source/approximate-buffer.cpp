#include "approximate-buffer.h"

//WAS LOCKED
ApproximateBuffer::ApproximateBuffer(const Range& bufferRange, const int64_t id, const int64_t creationPeriod, const size_t dataSizeInBytes, const InjectionConfigurationReference& injectorCfg) : 
	TrackingBuffer<false>(bufferRange, id, creationPeriod, dataSizeInBytes, injectorCfg.GetBitDepth(), injectorCfg.GetConfigurationId()),	

	m_minimumReadBackupSize(static_cast<size_t>(std::ceil(static_cast<double>(injectorCfg.GetBitDepth()) / static_cast<double>(BYTE_SIZE)))),

	#if DISTANCE_BASED_FAULT_INJECTOR
		m_faultInjector(injectorCfg, dataSizeInBytes)
	#else
		m_faultInjector(injectorCfg)
	#endif
{
	ApproximateBuffer::InitializeRecordsAndBackups(creationPeriod);
}

//MUST LOCK
void ApproximateBuffer::InitializeRecordsAndBackups(const int64_t period) {
	#if ENABLE_PASSIVE_INJECTION
		#if !DISTANCE_BASED_FAULT_INJECTOR
			using namespace BorrowedMemory;
			const LastAccessPeriodPool::iterator accessIt = g_lastAccessPeriodPool.find(this->GetNumberOfElements());
			if (accessIt != g_lastAccessPeriodPool.cend()) {
				this->m_lastAccessPeriod = std::unique_ptr<int64_t[]>(accessIt->second.release());
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
				
				this->m_lastAccessPeriod = std::unique_ptr<int64_t[]>(new int64_t[this->GetNumberOfElements()]);
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
				BorrowedMemory::g_lastAccessPeriodPool.insert({this->GetNumberOfElements(), std::unique_ptr<int64_t[]>(this->m_lastAccessPeriod.release())});
			} else {
				this->m_lastAccessPeriod.reset();
			}
	#endif
}

int64_t ApproximateBuffer::GetConfigurationId() const {
	return this->m_faultInjector.GetConfigurationId();
}

//WAS LOCKED
ApproximateBuffer::~ApproximateBuffer() {
	TrackingBuffer<false>::~TrackingBuffer();
	//this->CleanLogs();
}

//MUST LOCK
//AND m_isActive MUST BE CHECKED
void ApproximateBuffer::ReactivateBuffer(const int64_t creationPeriod) {
	#if MULTIPLE_BER_CONFIGURATION
		this->m_faultInjector.ResetBerIndex(creationPeriod);
	#endif

	ApproximateBuffer::InitializeRecordsAndBackups(creationPeriod);

	//this->m_creationPeriod = creationPeriod;

	TrackingBuffer<false>::ReactivateBuffer(creationPeriod);
}

size_t ApproximateBuffer::GetBitDepth() const {
	return this->m_faultInjector.GetBitDepth();
}

//WAS LOCKED
void ApproximateBuffer::NextPeriod(const int64_t period) {
	#if ENABLE_PASSIVE_INJECTION && DISTANCE_BASED_FAULT_INJECTOR 
		if (this->m_faultInjector.isInjectable(ErrorCategory::Passive)) {
			this->m_faultInjector.InjectFault(this->m_initialAddress, ErrorCategory::Passive, this->GetSoftwareBufferSSizeInBytes(), nullptr IF_COMMA_LOGGING_FAULTS(this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Passive)));
			this->m_lastPassiveInjectionPeriod = period;
		}
	#endif

	//this->StoreCurrentPeriodLog();

	#if MULTIPLE_BER_CONFIGURATION
		this->m_faultInjector.AdvanceBerIndex();
	#endif

	TrackingBuffer<false>::NextPeriod(period);

	//this->m_periodLog.ResetCounts(period, this->GetBitDepth());
}

int64_t ApproximateBuffer::GetCurrentPassiveBerMarker() const {
	return g_currentPeriod;
}

size_t ApproximateBuffer::GetTotalNecessaryReadBackupSize() const {
	return this->GetNumberOfElements() * this->m_minimumReadBackupSize;
}

//MUST LOCK
bool ApproximateBuffer::GetShouldInject(const size_t errorCat, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) const {
	return isThreadInjectionEnabled IF_PIN_LOCKED(&& isBufferInThread) && this->m_faultInjector.isInjectable(errorCat); 
}

const InjectionConfigurationReference& ApproximateBuffer::GetInjectionConfigurationReference() const {
	return this->m_faultInjector.GetReferenceConfiguration();
}

#if ENABLE_PASSIVE_INJECTION
	#if LOG_FAULTS
		//MUST LOCK
		uint64_t* ApproximateBuffer::GetPassiveErrorsLogFromIterator(const BufferLogs<false>::const_iterator& it) const {
			if (it != this->m_bufferLogs.cend()) { //TODO: CHECK m_periodLog FIRST!!!
				return it->second->GetErrorCountsByBit(ErrorCategory::Passive);
			} else {
				return this->m_periodLog.GetErrorCountsByBit(ErrorCategory::Passive);
			}
		}

		//MUST LOCK
		void ApproximateBuffer::AdvanceBufferLogIterator(BufferLogs<false>::const_iterator& it) const {
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
			const int64_t currentMarker = this->GetCurrentPassiveBerMarker();
			
			#if OVERCHARGE_BER
				int64_t& initialMarker = this->m_lastAccessPeriod[elementIndex];
				
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
				int64_t& initialMarker = this->m_lastAccessPeriod[elementIndex];

				#if LOG_FAULTS
					BufferLogs<false>::const_iterator it = this->m_bufferLogs.find(initialMarker);
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