#ifndef PINTOOL_CONTROL_H
#define PINTOOL_CONTROL_H

#include "approx-globals.h"
#include "thread-control.h"

namespace PintoolControl {
    extern GeneralBuffers generalBuffers;
    extern ThreadControl g_mainThreadControl;

    #if PIN_LOCKED 
        extern ThreadControlMap threadControlMap;  
    #endif

    VOID enable_global_injection(IF_PIN_LOCKED(const THREADID threadId));
    VOID disable_global_injection(IF_PIN_LOCKED(const THREADID threadId));
    VOID disable_access_instrumentation();
    VOID start_level(IF_PIN_LOCKED_COMMA(const THREADID threadId) const int64_t level);
    VOID end_level(IF_PIN_LOCKED(const THREADID threadId));
    VOID next_period();
    VOID add_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const int64_t bufferId, const int64_t configurationId, const uint32_t dataSizeInBytes);
    VOID remove_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const bool giveAwayRecords);

    #if PIN_LOCKED
        VOID ThreadStart(const THREADID threadId, CONTEXT * ctxt, const INT32 flags, VOID * v);
        VOID ThreadFini(const THREADID threadId, CONTEXT const * const ctxt, const INT32 code, VOID * v);
    #endif
}

#endif /* PINTOOL_CONTROL_H */