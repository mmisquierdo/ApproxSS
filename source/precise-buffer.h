#ifndef PRECISE_BUFFER_H
#define PRECISE_BUFFER_H

#include "range.h"
#include "period-log.h"
#include "injector-configuration.h"
#include "tracking-buffer.h"

class PreciseBuffer : virtual public TrackingBuffer<true> {
	protected:
		const int64_t m_configurationId;
		const size_t m_bitDepth;

	public:
		PreciseBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
					  const InjectionConfigurationReference& injectorCfg);

		PreciseBuffer(const PreciseBuffer&) = delete;
		PreciseBuffer(const PreciseBuffer&&) = delete;

		~PreciseBuffer();
		
		int64_t GetConfigurationId() const override;
        size_t GetBitDepth() const override;

		bool RetireBuffer(const bool giveAwayRecords) override; //return true if it's retired

		const InjectionConfigurationReference& GetInjectionConfigurationReference() const override;

		void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;
		void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) override;	

		//virtual void NextPeriod(const int64_t period);
		//virtual void ReactivateBuffer(const int64_t creationPeriod);
		//virtual bool RetireBuffer(const bool giveAwayRecords);
};

#endif /* PRECISE_BUFFER_H */