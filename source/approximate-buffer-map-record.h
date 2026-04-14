#ifndef APPROXIMATE_BUFFER_MAP_RECORD_H
#define APPROXIMATE_BUFFER_MAP_RECORD_H

/* ==================================================================== */
/* Short Term Approximate Buffer										*/
/* ==================================================================== */

#include "approximate-buffer.h"

#if LSB_DROPPING
typedef std::map<uint8_t* const, std::pair<bool, std::unique_ptr<uint8_t[]>>> RemainingReads; // bool is LSB_DROPPING
#else
typedef std::map<uint8_t* const, uint8_t*> RemainingReads;
#endif

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

class ApproximateBufferMapRecord : virtual public ApproximateBuffer {
	protected: 
		PendingWrites m_pendingWrites;
		RemainingReads m_remainingReads;
	
		// not proud of this
		#define RemainingReadsIterator IF_LSBDROPPED_ELSE(RemainingReads::iterator, RemainingReads::const_iterator)

		RemainingReadsIterator m_readHint;

		PendingWrites::const_iterator ApplyFaultyWrite(const PendingWrites::const_iterator it);
		void ApplyFaultyWrite(uint8_t * const accessedAddress);
		void ApplyFaultyWrite(uint8_t * const initialAddress, uint8_t const * const finalAddress);
		void ApplyAllWriteErrors();
		void RecordFaultyWrite(uint8_t* const address, PendingWrites::const_iterator& hint);

		RemainingReadsIterator ReverseFaultyRead(const RemainingReadsIterator it 									IF_COMMA_LSBDROPPED(const bool reverseLSBDrop = true));
		RemainingReadsIterator ReverseFaultyRead(uint8_t * const accessedAddess 									IF_COMMA_LSBDROPPED(const bool reverseLSBDrop = true));
		RemainingReadsIterator ReverseFaultyRead(uint8_t * const initialAddress, uint8_t const * const finalAddress IF_COMMA_LSBDROPPED(const bool reverseLSBDrop = true));
		void ReverseAllReadErrors();

		RemainingReads::const_iterator InvalidateRemainingRead(const RemainingReads::const_iterator it);
		void InvalidateRemainingRead(uint8_t * const accessedAddress);
		void InvalidateRemainingRead(uint8_t * const initialAddress, uint8_t const * const finalAddress);

		static uint8_t* GetWriteAddressFromIterator(const PendingWrites::const_iterator& it);
		auto GetWriteBerFromIterator(const PendingWrites::const_iterator& it);

		#if LOG_FAULTS
			static uint64_t* GetWriteErrorsLogFromIterator(const PendingWrites::const_iterator& it);
		#endif

		#if LSB_DROPPING
			bool IsBackedUp(uint8_t const * const targetAddress);
		#endif

		virtual void HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
		virtual void HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
	
	public:
		ApproximateBufferMapRecord(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
									const InjectionConfigurationReference& injectorCfg);
		~ApproximateBufferMapRecord();

		virtual void BackupReadData(uint8_t* const data IF_COMMA_LSBDROPPED(const bool isLSBDrop = false));

		virtual void ReactivateBuffer(const uint64_t creationPeriod);
		virtual bool RetireBuffer(const bool giveAwayRecords);
		virtual void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
		virtual void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
		virtual void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
		virtual void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
		virtual void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
		virtual void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread));
};

#endif /* APPROXIMATE_BUFFER_MAP_RECORD_H */