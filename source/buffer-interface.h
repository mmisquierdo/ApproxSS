#ifndef BUFFER_INTERFACE_H
#define BUFFER_INTERFACE_H

#include "range.h"
#include "period-log.h"

class BufferInterface {
public:
    virtual ~BufferInterface() = default;

    virtual int64_t GetConfigurationId() const = 0;
    virtual size_t GetBitDepth() const = 0;
    virtual int64_t GetBufferId() const = 0;
    virtual const InjectionConfigurationReference& GetInjectionConfigurationReference() const = 0;

    virtual void NextPeriod(const int64_t period) = 0;
    virtual void ReactivateBuffer(const int64_t creationPeriod) = 0;
    virtual bool RetireBuffer(const bool giveAwayRecords) = 0; 

    virtual void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
    virtual void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
    virtual void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
    virtual void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
    virtual void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
    virtual void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;  

    virtual void WriteLogHeaderToFile(std::ofstream& outputLog, const std::string& basePadding = "") const = 0;
    virtual void WriteAccessLogToFile(std::ofstream& outputLog, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& totalTargetAccessesBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding = "") const = 0;
    virtual void WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& totalTargetEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const std::string& basePadding = "") const = 0;
};

#endif /* BUFFER_INTERFACE_H */