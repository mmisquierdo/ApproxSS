#ifndef APPROXIMATE_BUFFER_H
#define APPROXIMATE_BUFFER_H

class FaultInjector;

#include "pin.H"

#include <string>
#include <set>
#include <map>
#include <unordered_map>
#include <stdlib.h> 
#include <algorithm>
#include <fstream>
#include <iostream>

#include "compiling-options.h"
#include "period-log.h"
#include "injector-configuration.h"
#include "fault-injector.h"
#include "consumption-profile.h"

//extern bool g_isGlobalInjectionEnabled;
//extern int g_level;
extern uint64_t g_currentPeriod;

class Range {
	public:
		uint8_t* const m_initialAddress;
		uint8_t const * const m_finalAddress;

		Range(uint8_t * const initialAddress, uint8_t const * const finalAddress) : m_initialAddress(initialAddress), m_finalAddress(finalAddress) {}

		ssize_t ssize() const {
			return this->m_finalAddress - this->m_initialAddress;
		}

		size_t size() const {
			return static_cast<size_t>(this->ssize());
		}

		bool IsEqual(const Range& other) const {
			return this->m_initialAddress == other.m_initialAddress && this->m_finalAddress == other.m_finalAddress;
		}

		bool DoesIntersectWith(uint8_t const * const address) const {
			return (address >= this->m_initialAddress && address < this->m_finalAddress);
		}

		bool DoesIntersectWith(const Range& other) const {
			return (this->m_initialAddress < other.m_finalAddress && this->m_finalAddress > other.m_initialAddress);
		}

		friend bool operator<(const Range& lhv, const Range& rhv) {  
			return lhv.m_finalAddress <= rhv.m_initialAddress; //m_finalAddress is not included
		} 
};

typedef std::map<size_t, const std::unique_ptr<PeriodLog>> BufferLogs;

class ApproximateBuffer : public Range {
	protected:
		const int64_t m_id;
		const size_t m_dataSizeInBytes;
		const size_t m_minimumReadBackupSize;
		uint64_t m_creationPeriod;
		//PIN_LOCK m_bufferLock;
		int32_t m_isActive;

		#if DISTANCE_BASED_FAULT_INJECTOR
			DistanceBasedFaultInjector m_faultInjector;
		#elif GRANULAR_FAULT_INJECTOR
			GranularFaultInjector m_faultInjector;
		#else
			FaultInjector m_faultInjector;
		#endif

		PeriodLog m_periodLog;
		BufferLogs m_bufferLogs;

		#if ENABLE_PASSIVE_INJECTION
			#if !DISTANCE_BASED_FAULT_INJECTOR
				std::unique_ptr<uint64_t[]> m_lastAccessPeriod;
				void UpdateLastAccessPeriod(uint8_t const * const initialAddress, const uint32_t accessSize);
				void UpdateLastAccessPeriod(uint8_t const * const accessedAddress);
				void UpdateLastAccessPeriod(const size_t elementIndex);
			#else
				uint64_t m_lastPassiveInjectionPeriod; 
			#endif

			void ApplyPassiveFault(const size_t elementIndex, uint8_t * const accessedAddress);
			void ApplyPassiveFault(uint8_t * const initialAddress, uint8_t const * const finalAddress);
			void ApplyPassiveFault(uint8_t * const accessedAddress);
			void ApplyAllPassiveErrors();

			#if LOG_FAULTS
				uint64_t* GetPassiveErrorsLogFromIterator(const BufferLogs::const_iterator& it) const;
				void AdvanceBufferLogIterator(BufferLogs::const_iterator& it) const;
			#endif
		#endif

		virtual void InitializeRecordsAndBackups(const uint64_t period);
		virtual void GiveAwayRecordsAndBackups(const bool giveAway);

		void StoreCurrentPeriodLog();
		void CleanLogs();

		uint64_t GetCurrentPassiveBerMarker() const;
		bool GetShouldInject(const size_t errorCat, const bool isThreadInjectionEnabled) const;

		size_t GetIndexFromAddress(uint8_t const * const address) const;
		size_t GetImplementationBufferSizeInBits() const;
		size_t GetNumberOfElements() const;
		size_t GetTotalNecessaryReadBackupSize() const;
		size_t GetSoftwareBufferSizeInBits() const;
		ssize_t GetSoftwareBufferSSizeInBytes() const;
		size_t GetSoftwareBufferSizeInBytes() const;
		bool IsIgnorableMisaligned(uint8_t const * const address, const uint32_t accessSize) const;
		bool IsMisaligned(uint8_t const * const address) const; 
		size_t GetAlignmentOffset(uint8_t const * const address) const;

		virtual void HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool shouldInject) = 0;
		virtual void HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool shouldInject) = 0;

	public:
		ApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
						  const InjectionConfigurationReference& injectorCfg);

		ApproximateBuffer(const ApproximateBuffer&) = delete;
		ApproximateBuffer(const ApproximateBuffer&&) = delete;

		virtual ~ApproximateBuffer();		

		virtual void BackupReadData(uint8_t* const data) = 0;

		void NextPeriod(const uint64_t period);
		virtual void ReactivateBuffer(const uint64_t creationPeriod);
		virtual bool RetireBuffer(const bool giveAwayRecords) = 0; //return true if it's retired
		virtual void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled) = 0;
		virtual void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled) = 0;
		virtual void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled) = 0;
		virtual void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled) = 0;
		virtual void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled) = 0;
		virtual void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled) = 0;
		
		int64_t GetConfigurationId() const;

		void WriteLogHeaderToFile(std::ofstream& outputLog, const std::string& basePadding = "") const;
		void WriteAccessLogToFile(std::ofstream& outputLog, std::array<uint64_t, AccessTypes::Size>& totalTargetAccessesBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding = "") const;
		void WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& totalTargetEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const std::string& basePadding = "") const;
};

/* ==================================================================== */
/* Short Term Approximate Buffer										*/
/* ==================================================================== */

typedef std::map<uint8_t* const, uint8_t*> RemainingReads;

#if MULTIPLE_BER_CONFIGURATION
	#if LOG_FAULTS
		#if !DISTANCE_BASED_FAULT_INJECTOR
			#if MULTIPLE_BER_ELEMENT
				typedef std::map<uint8_t* const, std::pair<double const *, uint64_t*>>				PendingWrites;
			#else
				typedef std::map<uint8_t* const, std::pair<double, uint64_t*>>						PendingWrites;
			#endif
		#else
			typedef std::map<uint8_t* const, std::pair<DistanceBasedInjectorRecord*, uint64_t*>>	PendingWrites;
		#endif
	#else
		#if !DISTANCE_BASED_FAULT_INJECTOR
			#if MULTIPLE_BER_ELEMENT
				typedef std::map<uint8_t* const, double const *>									PendingWrites;
			#else
				typedef std::map<uint8_t* const, double>											PendingWrites;
			#endif
		#else
			typedef std::map<uint8_t* const, DistanceBasedInjectorRecord*>							PendingWrites;
		#endif
	#endif
#else
	#if LOG_FAULTS
		typedef std::map<uint8_t* const, uint64_t*>													PendingWrites;
	#else
		typedef std::set<uint8_t* const>															PendingWrites;
	#endif
#endif

class ShortTermApproximateBuffer : virtual public ApproximateBuffer {
	protected: 
		PendingWrites m_pendingWrites;
		RemainingReads m_remainingReads;
		RemainingReads::const_iterator m_readHint;

		PendingWrites::const_iterator ApplyFaultyWrite(const PendingWrites::const_iterator it);
		void ApplyFaultyWrite(uint8_t * const accessedAddress);
		void ApplyFaultyWrite(uint8_t * const initialAddress, uint8_t const * const finalAddress);
		void ApplyAllWriteErrors();
		void RecordFaultyWrite(uint8_t* const address, PendingWrites::const_iterator& hint);
		RemainingReads::const_iterator ReverseFaultyRead(const RemainingReads::const_iterator it);
		RemainingReads::const_iterator ReverseFaultyRead(uint8_t * const accessedAddess);
		RemainingReads::const_iterator ReverseFaultyRead(uint8_t * const initialAddress, uint8_t const * const finalAddress);
		void ReverseAllReadErrors();
		RemainingReads::const_iterator InvalidateRemainingRead(const RemainingReads::const_iterator it);
		void InvalidateRemainingRead(uint8_t * const accessedAddress);
		void InvalidateRemainingRead(uint8_t * const initialAddress, uint8_t const * const finalAddress);

		static uint8_t* GetWriteAddressFromIterator(const PendingWrites::const_iterator& it);
		auto GetWriteBerFromIterator(const PendingWrites::const_iterator& it);

		#if LOG_FAULTS
			static uint64_t* GetWriteErrorsLogFromIterator(const PendingWrites::const_iterator& it);
		#endif

		virtual void HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool shouldInject);
		virtual void HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool shouldInject);
	
	public:
		ShortTermApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
									const InjectionConfigurationReference& injectorCfg);
		~ShortTermApproximateBuffer();

		virtual void BackupReadData(uint8_t* const data);
		virtual void ReactivateBuffer(const uint64_t creationPeriod);
		virtual bool RetireBuffer(const bool giveAwayRecords);
		virtual void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled);
};

/* ==================================================================== */
/* Long Term Approximate Buffer											*/
/* ==================================================================== */

struct ErrorStatus {
	static constexpr uint8_t None = 0;
	static constexpr uint8_t Read = 1;
	static constexpr uint8_t Write = 2;
};

class InjectionRecord {
	public: 
		uint8_t errorStatus;

	InjectionRecord() {
		this->errorStatus = ErrorStatus::None;
	}
};

#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
	class WriteSupportRecord {
		public:
			#if MULTIPLE_BER_CONFIGURATION
				#if !DISTANCE_BASED_FAULT_INJECTOR
					#if MULTIPLE_BER_ELEMENT
						double const * writeSupport;
					#else
						double writeSupport;
					#endif
				#else
					DistanceBasedInjectorRecord* writeSupport;
				#endif
			#endif

			#if LOG_FAULTS
				uint64_t* writeErrorsCountByBit;
			#endif
	};
#endif

namespace BorrowedMemory {
	//#if LONG_TERM_BUFFER
		typedef std::unordered_multimap<size_t, std::unique_ptr<InjectionRecord[]>> InjectionRecordPool;
		typedef std::unordered_multimap<size_t, std::unique_ptr<uint8_t[]>> ReadBackupsPool;

		extern InjectionRecordPool g_injectionRecords;
		extern ReadBackupsPool g_readBackups;

		#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
			typedef std::unordered_multimap<size_t, std::unique_ptr<WriteSupportRecord[]>> WriteSupportRecordPool;
			extern WriteSupportRecordPool g_writeSupportRecordPool;
		#endif
	//#endif

	#if ENABLE_PASSIVE_INJECTION && !DISTANCE_BASED_FAULT_INJECTOR
		typedef std::unordered_multimap<size_t, std::unique_ptr<uint64_t[]>> LastAccessPeriodPool;
		extern LastAccessPeriodPool g_lastAccessPeriodPool;
	#endif
}

class LongTermApproximateBuffer : virtual public ApproximateBuffer {
	protected: 
		std::unique_ptr<InjectionRecord[]> m_records;
		std::unique_ptr<uint8_t[]> m_readBackups;

		#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
			std::unique_ptr<WriteSupportRecord[]> m_writeSupportRecords;
		#endif

		uint8_t* GetBackupAddressFromIndex(const size_t index) const;

		virtual void InitializeRecordsAndBackups(const uint64_t period);
		virtual void GiveAwayRecordsAndBackups(const bool giveAway);


		void ApplyWriteFault(const size_t elementIndex, uint8_t* const accessedAddress);
		void ReverseFaultyRead(const size_t elementIndex, uint8_t* const accessedAddress);

		void RecordFaultyWrite(const size_t elementIndex);

		auto GetWriteBer(const size_t elementIndex);

		void ProcessWrittenMemoryElement(const size_t elementIndex, const uint8_t newStatus, const bool shouldInject);
		void ProcessReadMemoryElement(const size_t elementIndex, uint8_t* const accessedAddress, const bool shouldInject);

		virtual void HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool shouldInject);
		virtual void HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool shouldInject);

	public:
		LongTermApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
								const InjectionConfigurationReference& injectorCfg);

		LongTermApproximateBuffer(const LongTermApproximateBuffer&) = delete;
		LongTermApproximateBuffer(const LongTermApproximateBuffer&&) = delete;

		~LongTermApproximateBuffer();
		
		virtual void BackupReadData(uint8_t* const data); 


		virtual void ReactivateBuffer(const uint64_t creationPeriod);
		virtual bool RetireBuffer(const bool giveAwayRecords);
		virtual void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled);
		virtual void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled);
};

#endif /* APPROXIMATE_BUFFER_H */