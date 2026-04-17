#include "thread-control.h"
#include <iostream>

ThreadControl::ThreadControl(const THREADID threadId) : m_threadId(threadId) {
    this->m_level = 0;
    this->m_injectionEnabled = true;

    #if MULTIPLE_ACTIVE_BUFFERS
        //this->m_activeBuffers();
    #else
        this->m_activeBuffer = nullptr;
    #endif
}

ThreadControl::~ThreadControl() {
    #if MULTIPLE_ACTIVE_BUFFERS
        for (ActiveBuffers::const_iterator it = this->m_activeBuffers.cbegin(); it != this->m_activeBuffers.cend(); ) { 
            BufferInterface& approxBuffer = *(it->second);
            approxBuffer.RetireBuffer(false);
            it = this->m_activeBuffers.erase(it);
        }
    #else
        if (this->m_activeBuffer != nullptr) {
            this->m_activeBuffer->RetireBuffer(false);
            this->m_activeBuffer = nullptr;
        }
    #endif
}

bool ThreadControl::isThreadInjectionEnabled() const {
    return this->m_level && m_injectionEnabled;
}

bool ThreadControl::HasActiveBuffer() const {
    return 
    #if MULTIPLE_ACTIVE_BUFFERS
        (!this->m_activeBuffers.empty())
    #else
        (this->m_activeBuffer)
    #endif
    ;
}

bool ThreadControl::IsPresent(const Range& range) const {
    #if MULTIPLE_ACTIVE_BUFFERS
        const ActiveBuffers::const_iterator it =  this->m_activeBuffers.find(range);
        if (it != this->m_activeBuffers.cend()) {
            return true;
        }
    #else
        if (this->m_activeBuffer != nullptr && this->m_activeBuffer->DoesIntersectWith(range)) {
            return true;
        }
    #endif
    return false;
}

uint64_t ThreadControl::HowManyActiveBuffers() const {
    #if MULTIPLE_ACTIVE_BUFFERS
        return this->m_activeBuffers.size();
    #else
        return this->m_activeBuffer ? 1 : 0;
    #endif
}

void ThreadControl::PrintStillActiveBuffers() const {
    #if MULTIPLE_ACTIVE_BUFFERS
        std::cout << "Still active buffer(s): ";
        std::cout << "\tIds: ";
        for (auto const& [range, buffer] : this->m_activeBuffers) {
            std::cout << '(' <<  buffer->GetBufferId() << ", " << buffer->GetConfigurationId() << "); ";
        }
        std::cout << std::endl;
    #else
        if (this->m_activeBuffer) {
            std::cout << '(' <<  this->m_activeBuffer->GetBufferId() << ", " << this->m_activeBuffer->GetConfigurationId() << "); ";
        }
    #endif
}