#ifndef TRACKING_BUFFER_H
#define TRACKING_BUFFER_H

#include "range.h"
#include "period-log.h"
#include "buffer-interface.h"

template<bool isPrecise = false>
using BufferLogs = std::map<size_t, std::unique_ptr<const PeriodLog<isPrecise>>>;

template <bool isPrecise = false>
class TrackingBuffer : public SizedRange, public BufferInterface {
    protected:
		const int64_t m_id;
		int32_t m_isActive;

		PeriodLog<isPrecise> m_periodLog;
		BufferLogs<isPrecise> m_bufferLogs;

        void ResetOrRestorePeriodLog(const int64_t period);
        void StoreCurrentPeriodLog();
		void CleanLogs();

    public:
        TrackingBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes, const size_t bitDepth, const int64_t configurationId);
        
        TrackingBuffer(const TrackingBuffer<isPrecise>&) = delete;
        
        ~TrackingBuffer();

        virtual int64_t GetConfigurationId() const = 0;
        virtual size_t GetBitDepth() const = 0;
		int64_t GetBufferId() const;

        virtual const InjectionConfigurationReference& GetInjectionConfigurationReference() const = 0;

        virtual void NextPeriod(const int64_t period);
		virtual void ReactivateBuffer(const int64_t creationPeriod);
		virtual bool RetireBuffer(const bool giveAwayRecords) = 0; //return true if it's retired

		virtual void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;	

		void WriteLogHeaderToFile(std::ofstream& outputLog, const std::string& basePadding = "") const;
		void WriteAccessLogToFile(std::ofstream& outputLog, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& totalTargetAccessesBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding = "") const;
		void WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& totalTargetEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const std::string& basePadding = "") const;
};

#endif /* TRACKING_BUFFER_H */