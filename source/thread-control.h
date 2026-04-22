#ifndef THREAD_CONTROL_H
#define THREAD_CONTROL_H

#include "approx-globals.h"

class ThreadControl {
    public: 
        const THREADID m_threadId;
        int64_t m_level;
        bool m_injectionEnabled;

        #if MULTIPLE_ACTIVE_BUFFERS
            ActiveBuffers m_activeBuffers;
        #else
            BufferInterface* m_activeBuffer;
        #endif

        ThreadControl(const THREADID threadId);
        ~ThreadControl();

        bool isThreadInjectionEnabled() const;
        bool HasActiveBuffer() const;
        bool IsPresent(const Range& range) const;
        uint64_t HowManyActiveBuffers() const;
        void PrintStillActiveBuffers() const;
};

typedef std::map<THREADID, std::unique_ptr<ThreadControl>> ThreadControlMap;

#endif /* THREAD_CONTROL_H */