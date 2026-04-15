#include "precise-buffer.h"

PreciseBuffer::~PreciseBuffer() {
    TrackingBuffer::~TrackingBuffer();
}

PreciseBuffer::PreciseBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes, const InjectionConfigurationReference& injectorCfg) : 
	TrackingBuffer<true>(bufferRange, id, creationPeriod, dataSizeInBytes, injectorCfg.GetBitDepth(), injectorCfg.GetConfigurationId()),	
    m_configurationId(injectorCfg.GetConfigurationId()),
    m_bitDepth(injectorCfg.GetBitDepth()) {}

int64_t PreciseBuffer::GetConfigurationId() const {
    return this->m_configurationId;
}

size_t PreciseBuffer::GetBitDepth() const {
    return this->m_bitDepth;
}

bool PreciseBuffer::RetireBuffer(const bool giveAwayRecords) {
    if (this->m_isActive >= 1) { //if there's at least one thread using it...
		this->m_isActive--;

		if (this->m_isActive == 0) { //failsafe against repeated retirements
			this->StoreCurrentPeriodLog();
			return true;
		} else {
			return false;
		}
	} else {
		return true;
	}
}

const InjectionConfigurationReference& PreciseBuffer::GetInjectionConfigurationReference() const {
    const auto it = g_injectorConfigurations.find(this->GetConfigurationId()); //WARNING: this also may crash the tool, but it should have crashed before

	return *(it->second.get());
}

void PreciseBuffer::HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(false IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, accessSize);
}

void PreciseBuffer::HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
    this->m_periodLog.IncreaseAccess(false IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, accessSize);
}

void PreciseBuffer::HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(false IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Write, this->m_dataSizeInBytes * memOpInfo->NumOfElements());
}

void PreciseBuffer::HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(false IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, accessSize);
}

void PreciseBuffer::HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
    this->m_periodLog.IncreaseAccess(false IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, accessSize);
}

void PreciseBuffer::HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) {
	this->m_periodLog.IncreaseAccess(false IF_COMMA_PIN_LOCKED(isBufferInThread), AccessTypes::Read, this->m_dataSizeInBytes * memOpInfo->NumOfElements());
}