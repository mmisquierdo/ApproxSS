#ifndef ACCESS_HANDLER_H
#define ACCESS_HANDLER_H

#include "approx-globals.h"
#include "thread-control.h"

namespace AccessHandler {
    VOID HandleMemoryReadSIMD(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes);
    VOID HandleMemoryRead(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes);
    VOID HandleMemoryWriteSIMD(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes);
    VOID HandleMemoryWrite(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes);
    
    VOID HandleMemoryReadScattered(IF_PIN_LOCKED_COMMA(const THREADID threadId) IMULTI_ELEMENT_OPERAND const * const memOpInfo);
    VOID HandleMemoryWriteScattered(IF_PIN_LOCKED_COMMA(const THREADID threadId) IMULTI_ELEMENT_OPERAND const * const memOpInfo);
}

#endif /* ACCESS_HANDLER_H */