#include "pintool-control.h"
#include <iostream>

namespace PintoolControl {
    GeneralBuffers g_generalBuffers;
    ThreadControl g_mainThreadControl(-1);

    #if PIN_LOCKED 
        ThreadControlMap threadControlMap;  
        static PIN_LOCK tcMap_lock;
    #endif

    VOID enable_global_injection(IF_PIN_LOCKED(const THREADID threadId)) {
        #if PIN_LOCKED
            ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId))); 
        #else
            ThreadControl& tdata = PintoolControl::g_mainThreadControl;
        #endif
        tdata.m_injectionEnabled = true;
    }

    VOID disable_global_injection(IF_PIN_LOCKED(const THREADID threadId)) {
        #if PIN_LOCKED
            ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
        #else
            ThreadControl& tdata = PintoolControl::g_mainThreadControl;
        #endif
        tdata.m_injectionEnabled = false;
    }

    VOID disable_access_instrumentation() {
        SET_ACCESS_INSTRUMENTATION_STATUS(false)
    }

    VOID start_level(IF_PIN_LOCKED_COMMA(const THREADID threadId) const int64_t level) {
        #if PIN_LOCKED
            ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
        #else
            ThreadControl& tdata = PintoolControl::g_mainThreadControl;
        #endif

        tdata.m_level++;

        StoreAccessLayer(g_layeredAccesses, g_accessCounter, g_levels, g_sequenceHash);
        #if BUFFERS_LAYERED_COUNTER
            StoreAccessLayer(g_buffersLayeredAccesses, g_buffersAccessCounter, g_levels, g_sequenceHash);
        #endif

        g_layerHashes.push(g_sequenceHash);
        g_levels.push_back(level);
        g_sequenceHash = HashValue(level, g_sequenceHash);
    }

    VOID end_level(IF_PIN_LOCKED(const THREADID threadId)) {
        #if PIN_LOCKED
            ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
        #else
            ThreadControl& tdata = PintoolControl::g_mainThreadControl;
        #endif

        tdata.m_level--;

        StoreAccessLayer(g_layeredAccesses, g_accessCounter, g_levels, g_sequenceHash);
        #if BUFFERS_LAYERED_COUNTER
            StoreAccessLayer(g_buffersLayeredAccesses, g_buffersAccessCounter, g_levels, g_sequenceHash);
        #endif

        g_sequenceHash = g_layerHashes.top();
        g_layerHashes.pop();
        g_levels.pop_back();
    }

    VOID next_period() {
        IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

        ++g_currentPeriod;

        ThreadControl& tdata = PintoolControl::g_mainThreadControl;

        #if MULTIPLE_ACTIVE_BUFFERS
            for (const auto& [_, activeBuffer] : tdata.m_activeBuffers) {
                activeBuffer->NextPeriod(g_currentPeriod);
            }
        #else
            if (tdata.m_activeBuffer != nullptr) {
                tdata.m_activeBuffer->NextPeriod(g_currentPeriod);
            }
        #endif

        IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
    }

    VOID add_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const int64_t bufferId, const int64_t configurationId, const uint64_t dataSizeInBytes, const int64_t isPrecise) {
        const Range range = Range(start_address, end_address-1);
        
        ThreadControl& mainThread = PintoolControl::g_mainThreadControl;

        IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

        #if MULTIPLE_ACTIVE_BUFFERS
            ActiveBuffers::const_iterator lbActiveMain = mainThread.m_activeBuffers.lower_bound(range);
            if (!((lbActiveMain != mainThread.m_activeBuffers.cend()) && !(mainThread.m_activeBuffers.key_comp()(range, lbActiveMain->first)))) 
        #else
            if (mainThread.m_activeBuffer == nullptr)
        #endif
        {
            const GeneralBufferRecord generalBufferKey = std::make_tuple(range.m_initialAddress, range.m_finalAddress, bufferId, configurationId, dataSizeInBytes, isPrecise);
            const GeneralBuffers::const_iterator lbGeneral = PintoolControl::g_generalBuffers.lower_bound(generalBufferKey);

            if ((lbGeneral != PintoolControl::g_generalBuffers.cend()) && !(PintoolControl::g_generalBuffers.key_comp()(generalBufferKey, lbGeneral->first))) {
                #if MULTIPLE_ACTIVE_BUFFERS
                    BufferInterface* const approxBuffer = lbGeneral->second.get();
                    approxBuffer->ReactivateBuffer(g_currentPeriod);
                    lbActiveMain = mainThread.m_activeBuffers.insert(lbActiveMain, {range, approxBuffer});
                #else
                    mainThread.m_activeBuffer = lbGeneral->second.get();
                    mainThread.m_activeBuffer->ReactivateBuffer(g_currentPeriod);
                #endif
            } else {
                const InjectorConfigurationMap::const_iterator bcIt = g_injectorConfigurations.find(configurationId);

                if (bcIt == g_injectorConfigurations.cend()) {
                    std::cerr << "ApproxSS Error: Configuration " << configurationId << " not found." << std::endl;
                    PIN_ExitProcess(EXIT_FAILURE);
                }

                BufferInterface* approxBuffer;

                if (isPrecise) {
                    approxBuffer = new PreciseBuffer(range, bufferId, g_currentPeriod, dataSizeInBytes, *bcIt->second);
                } else {
                    approxBuffer = new ChosenTermApproximateBuffer(range, bufferId, g_currentPeriod, dataSizeInBytes, *bcIt->second);
                }

                #if MULTIPLE_ACTIVE_BUFFERS
                    lbActiveMain = mainThread.m_activeBuffers.insert(lbActiveMain, {range, approxBuffer});
                #else
                    mainThread.m_activeBuffer = approxBuffer;
                #endif

                PintoolControl::g_generalBuffers.emplace_hint(lbGeneral, generalBufferKey, std::unique_ptr<BufferInterface>(approxBuffer));
            }
        } 
        #if !PIN_LOCKED
            else {
                std::cout << "ApproxSS Warning: approximate buffer (id: " << bufferId << ") already active. Ignoring addition request." << std::endl;
            }
        #endif

        {
            #if PIN_LOCKED 
                ThreadControl& localThread = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));

                #if MULTIPLE_ACTIVE_BUFFERS
                    const ActiveBuffers::const_iterator lbActiveLocal = localThread.m_activeBuffers.lower_bound(range);
                    if (!((lbActiveLocal != localThread.m_activeBuffers.cend()) && !(localThread.m_activeBuffers.key_comp()(range, lbActiveLocal->first)))) { 
                        BufferInterface* const approxBuffer = lbActiveMain->second;
                        approxBuffer->ReactivateBuffer(g_currentPeriod);
                        localThread.m_activeBuffers.insert(lbActiveLocal, {range, approxBuffer});
                    }
                #else
                    if (localThread.m_activeBuffer == nullptr) {
                        localThread.m_activeBuffer = mainThread.m_activeBuffer;
                        localThread.m_activeBuffer->ReactivateBuffer(g_currentPeriod);
                    }
                #endif
                  else {
                    std::cout << "ApproxSS Warning: approximate buffer (id: " << bufferId << ") already active in thread " << threadId << ". Ignoring addition request." << std::endl;
                }
            #endif
        }

        IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
    }

    VOID remove_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const bool giveAwayRecords) {
        const Range range = Range(start_address, end_address-1);
        ThreadControl& mainThread = PintoolControl::g_mainThreadControl;    

        IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

        {
        #if PIN_LOCKED
            ThreadControl& localThread = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId))); 

            #if MULTIPLE_ACTIVE_BUFFERS
                const ActiveBuffers::const_iterator lbActive = localThread.m_activeBuffers.find(range); 
                if (lbActive != localThread.m_activeBuffers.cend() && lbActive->first.IsEqual(range)){
                    lbActive->second->RetireBuffer(giveAwayRecords);
                    localThread.m_activeBuffers.erase(lbActive);
                }
            #else
                if (localThread.m_activeBuffer != nullptr && localThread.m_activeBuffer->IsEqual(range)) {
                    localThread.m_activeBuffer->RetireBuffer(giveAwayRecords);
                    localThread.m_activeBuffer = nullptr;
                }
            #endif
              else {
                std::cout << "ApproxSS Warning: approximate buffer not found for removal in thread " << threadId << ". Ignorning request." << std::endl;
            }
        #endif
        }
    
        #if MULTIPLE_ACTIVE_BUFFERS
            const ActiveBuffers::const_iterator lbActive = mainThread.m_activeBuffers.find(range); 
            if (lbActive != mainThread.m_activeBuffers.cend() && lbActive->first.IsEqual(range)){
                if (lbActive->second->RetireBuffer(giveAwayRecords)) {
                    mainThread.m_activeBuffers.erase(lbActive); 
                }
            }
        #else
            if (mainThread.m_activeBuffer != nullptr && mainThread.m_activeBuffer->IsEqual(range)) {
                if (mainThread.m_activeBuffer->RetireBuffer(giveAwayRecords)) {
                    mainThread.m_activeBuffer = nullptr;
                }
            }
        #endif
        #if !PIN_LOCKED
              else {
                std::cout << "ApproxSS Warning: approximate buffer [" << (size_t) start_address << "; " << (size_t) end_address << "] not found for removal. Ignorning request." << std::endl;
            }
        #endif

        IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
    }

    #if PIN_LOCKED
        VOID ThreadStart(const THREADID threadId, CONTEXT * ctxt, const INT32 flags, VOID * v) {
            std::cout << std::endl << "Target application thread STARTED. Id: " << threadId  << std::endl;

            PIN_GetLock(&tcMap_lock, threadId); 
            const std::pair<const ThreadControlMap::const_iterator, const bool> it = PintoolControl::threadControlMap.insert({threadId, std::make_unique<ThreadControl>(threadId)});
            PIN_ReleaseLock(&tcMap_lock);

            if (PIN_SetThreadData(g_tlsKey, it.first->second.get(), threadId) == FALSE) {
                std::cerr << "Pin Error: PIN_SetThreadData failed" << std::endl;
                PIN_ExitProcess(EXIT_FAILURE);
            }
        }
        
        VOID ThreadFini(const THREADID threadId, CONTEXT const * const ctxt, const INT32 code, VOID * v) {
            ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));

            std::cout << std::endl << "Target application thread ENDED: " << threadId << ". Final level: " << tdata.m_level << std::endl;

            PIN_GetLock(&tcMap_lock, threadId); 
            PintoolControl::threadControlMap.erase(threadId);
            PIN_ReleaseLock(&tcMap_lock);
        }
    #endif
}