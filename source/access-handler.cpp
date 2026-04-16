#include "access-handler.h"
#include "pintool-control.h"

namespace AccessHandler {
    static const ThreadControl& GetInterestThreadControl(IF_PIN_LOCKED(const THREADID threadId)) {
        #if PIN_LOCKED
            return *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
        #else
            return PintoolControl::g_mainThreadControl;
        #endif
    }

    #if PIN_LOCKED
        static bool IsPresent(IF_PIN_LOCKED_COMMA(const ThreadControl& threadControl) IF_PIN_LOCKED(const Range& range)) {
            #if PIN_LOCKED
                return threadControl.IsPresent(range);
            #else
                return true;
            #endif
        }
    #endif

    VOID CheckAndForward(IF_PIN_LOCKED_COMMA(const THREADID threadId) void (ChosenTermApproximateBuffer::*function)(uint8_t* const, const UINT32, const bool IF_COMMA_PIN_LOCKED(const bool)), uint8_t* const accessedAddress, const UINT32 accessSizeInBytes IF_COMMA_BUFFER_LAYERED(const size_t accessType)) {
        #if PIN_LOCKED
            if (!PintoolControl::g_mainThreadControl.HasActiveBuffer()) {
                return;
            }
        #endif

        IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

        const ThreadControl& mainThread = PintoolControl::g_mainThreadControl;

        #if MULTIPLE_ACTIVE_BUFFERS || PIN_LOCKED
            const Range range = Range(accessedAddress, accessedAddress);
        #endif

        #if MULTIPLE_ACTIVE_BUFFERS
            const ActiveBuffers::const_iterator it =  mainThread.m_activeBuffers.find(range);
            if (it != mainThread.m_activeBuffers.cend()) {
                ChosenTermApproximateBuffer& approxBuffer = *(it->second);
                const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
                (approxBuffer.*function)(accessedAddress, accessSizeInBytes, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
            
                #if BUFFERS_LAYERED_COUNTER
                    g_buffersAccessCounter[accessType] += accessSizeInBytes;
                #endif
            }
        #else
            if (mainThread.m_activeBuffer != nullptr && mainThread.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
                ChosenTermApproximateBuffer& approxBuffer = *(mainThread.m_activeBuffer);
                const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
                (approxBuffer.*function)(accessedAddress, accessSizeInBytes, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
            
                #if BUFFERS_LAYERED_COUNTER
                    g_buffersAccessCounter[accessType] += accessSizeInBytes;
                #endif
            }
        #endif

        IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
    }

    VOID HandleMemoryReadSIMD(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
        g_accessCounter[AccessTypes::Read] += accessSizeInBytes;
        CheckAndForward(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSIMD, accessedAddress, accessSizeInBytes IF_COMMA_BUFFER_LAYERED(AccessTypes::Read));
    }

    VOID HandleMemoryRead(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {    
        g_accessCounter[AccessTypes::Read] += accessSizeInBytes;    
        CheckAndForward(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSingleElementSafe, accessedAddress, accessSizeInBytes IF_COMMA_BUFFER_LAYERED(AccessTypes::Read));
    }

    VOID HandleMemoryWriteSIMD(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
        g_accessCounter[AccessTypes::Write] += accessSizeInBytes;
        CheckAndForward(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteSIMD, accessedAddress, accessSizeInBytes IF_COMMA_BUFFER_LAYERED(AccessTypes::Write));
    }

    VOID HandleMemoryWrite(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
        g_accessCounter[AccessTypes::Write] += accessSizeInBytes;
        CheckAndForward(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteSingleElementSafe, accessedAddress, accessSizeInBytes IF_COMMA_BUFFER_LAYERED(AccessTypes::Write));
    }

    VOID CheckAndForwardScattered(IF_PIN_LOCKED_COMMA(const THREADID threadId) void (ChosenTermApproximateBuffer::*function)(IMULTI_ELEMENT_OPERAND const * const, const bool IF_COMMA_PIN_LOCKED(const bool)), IMULTI_ELEMENT_OPERAND const * const memOpInfo IF_COMMA_BUFFER_LAYERED(const size_t accessType)) {
        #if PIN_LOCKED
            if (!PintoolControl::g_mainThreadControl.HasActiveBuffer()) {
                return;
            }
        #endif

        if (memOpInfo->NumOfElements() < 1) {
            return;
        }
        
        uint8_t * accessedAddress = (uint8_t*) memOpInfo->ElementAddress(0); 
        ThreadControl& mainThread = PintoolControl::g_mainThreadControl;

        #if MULTIPLE_ACTIVE_BUFFERS || PIN_LOCKED
            const Range range = Range(accessedAddress, accessedAddress); 
        #endif
        
        IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)
        
        #if MULTIPLE_ACTIVE_BUFFERS
            const ActiveBuffers::const_iterator it = mainThread.m_activeBuffers.find(range);
            if (it != mainThread.m_activeBuffers.cend()) {
                ChosenTermApproximateBuffer& approxBuffer = *(it->second);

                const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));

                (approxBuffer.*function)(memOpInfo, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
            
                #if BUFFERS_LAYERED_COUNTER
                    g_buffersAccessCounter[accessType] += memOpInfo->NumOfElements() * memOpInfo->ElementSize(0);
                #endif
            }
        #else
            if (mainThread.m_activeBuffer != nullptr && mainThread.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
                ChosenTermApproximateBuffer& approxBuffer = *(mainThread.m_activeBuffer);

                const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));

                (approxBuffer.*function)(memOpInfo, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
            
                #if BUFFERS_LAYERED_COUNTER
                    g_buffersAccessCounter[accessType] += memOpInfo->NumOfElements() * memOpInfo->ElementSize(0);
                #endif
            }
        #endif

        IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
    }

    VOID HandleMemoryReadScattered(IF_PIN_LOCKED_COMMA(const THREADID threadId) IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
        g_accessCounter[AccessTypes::Read] += memOpInfo->NumOfElements() * memOpInfo->ElementSize(0);
        CheckAndForwardScattered(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadScattered, memOpInfo IF_COMMA_BUFFER_LAYERED(AccessTypes::Read));
    }

    VOID HandleMemoryWriteScattered(IF_PIN_LOCKED_COMMA(const THREADID threadId) IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
        g_accessCounter[AccessTypes::Write] += memOpInfo->NumOfElements() * memOpInfo->ElementSize(0);
        CheckAndForwardScattered(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteScattered, memOpInfo IF_COMMA_BUFFER_LAYERED(AccessTypes::Write));
    }
}