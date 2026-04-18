#ifndef APPROXIMATE_BUFFER_ARRAY_RECORD_H
#define APPROXIMATE_BUFFER_ARRAY_RECORD_H

/* ==================================================================== */
/* Long Term Approximate Buffer											*/
/* ==================================================================== */

#include "approximate-buffer.h"


struct ErrorStatus {
	static constexpr uint8_t None = 	0;
	static constexpr uint8_t Read = 	1 << 0;
	static constexpr uint8_t Write =	1 << 1;
	static constexpr uint8_t LSBDrop = 	1 << 2;
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

class ApproximateBufferArrayRecord : public ApproximateBuffer {
	protected: 
		std::unique_ptr<InjectionRecord[]> m_records;
		std::unique_ptr<uint8_t[]> m_readBackups;

		#if MULTIPLE_BER_CONFIGURATION || LOG_FAULTS
			std::unique_ptr<WriteSupportRecord[]> m_writeSupportRecords;
		#endif

		size_t m_highestInjectedElement;
		size_t m_lowestInjectedElement;

		uint8_t* GetBackupAddressFromIndex(const size_t index) const;

		void InitializeRecordsAndBackups(const int64_t period) override;
		void GiveAwayRecordsAndBackups(const bool giveAway) override;


		void ApplyWriteFault(const size_t elementIndex, uint8_t* const accessedAddress);
		void ReverseFaultyRead(const size_t elementIndex, uint8_t* const accessedAddress);

		void RecordFaultyWriteSupport(const size_t elementIndex);

		auto GetWriteBer(const size_t elementIndex);

		void ProcessWrittenMemoryElement(const size_t elementIndex, const uint8_t newStatus, const bool shouldInject);
		void ProcessReadMemoryElement(const size_t elementIndex, uint8_t* const accessedAddress, const bool shouldInject IF_COMMA_LSBDROPPED(const bool reverseLSBDrop = true));

		void HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
		void HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));

	public:
		ApproximateBufferArrayRecord(const Range& bufferRange, const int64_t id, const int64_t creationPeriod, const size_t dataSizeInBytes,
								const InjectionConfigurationReference& injectorCfg);

		ApproximateBufferArrayRecord(const ApproximateBufferArrayRecord&) = delete;
		ApproximateBufferArrayRecord(const ApproximateBufferArrayRecord&&) = delete;

		~ApproximateBufferArrayRecord();
		
		void BackupReadData(uint8_t* const data IF_COMMA_LSBDROPPED(const bool isLSBDrop = false)) override;

		void ReactivateBuffer(const int64_t creationPeriod) override;
		bool RetireBuffer(const bool giveAwayRecords) override;
		void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
};

#endif /* APPROXIMATE_BUFFER_ARRAY_RECORD_H */