/*
 *  This file contains an ISA-portable PIN tool for injecting memory faults.
 */

#include <stdio.h>
#include "pin.H"
#include <set>
#include <map>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unistd.h>
#include <iomanip>
#include <ctime>
#include <sstream>
#include "approximate-buffer.h"
#include "configuration-input.h"
#include "compiling-options.h"

//bool g_isGlobalInjectionEnabled = true;
//int g_level = 0;
uint64_t g_injectionCalls = 0; //NOTE: possible race condition, but I don't care
uint64_t g_currentPeriod = 0;

PIN_LOCK g_pinLock;
TLS_KEY g_tlsKey = INVALID_TLS_KEY;

#if NARROW_ACCESS_INSTRUMENTATION
	bool IsInstrumentationActive = false;
	#define ASSERT_ACCESS_INSTRUMENTATION_ACTIVE() if (!IsInstrumentationActive) return; 
	#define SET_ACCESS_INSTRUMENTATION_STATUS(stat) IsInstrumentationActive = stat;
#else
	#define ASSERT_ACCESS_INSTRUMENTATION_ACTIVE()
	#define SET_ACCESS_INSTRUMENTATION_STATUS(stat)
#endif

///////////////////////////////////////////////////////

#if LONG_TERM_BUFFER
	typedef LongTermApproximateBuffer ChosenTermApproximateBuffer;
#else
	typedef ShortTermApproximateBuffer ChosenTermApproximateBuffer;
#endif

typedef std::tuple<uint8_t const *, uint8_t const *, int64_t, int64_t, size_t> GeneralBufferRecord; //<Range, BufferId, ConfigurationId, dataSizeInBytes>
typedef std::map<GeneralBufferRecord, const std::unique_ptr<ChosenTermApproximateBuffer>> GeneralBuffers; 

#if MULTIPLE_ACTIVE_BUFFERS
	struct RangeCompare {
		//overlapping ranges are considered equivalent
		bool operator()(const Range& lhv, const Range& rhv) const {  
			return lhv.m_finalAddress <= rhv.m_initialAddress;
		} 
	};
	typedef std::map<Range, ChosenTermApproximateBuffer*, RangeCompare> ActiveBuffers;
#endif

class ThreadControl {
	public: 
		const THREADID m_threadId;
		int64_t m_level;
		bool m_injectionEnabled;

		#if MULTIPLE_ACTIVE_BUFFERS
			ActiveBuffers m_activeBuffers;
		#else
			ChosenTermApproximateBuffer* m_activeBuffer;
		#endif

	ThreadControl(const THREADID threadId) : m_threadId(threadId) {
		this->m_level = 0;
		this->m_injectionEnabled = true;

		#if MULTIPLE_ACTIVE_BUFFERS
			//this->m_activeBuffers();
		#else
			this->m_activeBuffer = nullptr;
		#endif
	}

	bool isThreadInjectionEnabled() const {
		return this->m_level && m_injectionEnabled;
	}

	~ThreadControl() {
		#if MULTIPLE_ACTIVE_BUFFERS
			for (ActiveBuffers::const_iterator it = this->m_activeBuffers.cbegin(); it != this->m_activeBuffers.cend(); ) { 
				ChosenTermApproximateBuffer& approxBuffer = *(it->second);
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
};

typedef std::map<THREADID, std::unique_ptr<ThreadControl>> ThreadControlMap;

/* ==================================================================== */
/* ApproxSS Control														*/
/* ==================================================================== */

InjectorConfigurationMap	g_injectorConfigurations; //todo: place them into the PintoolControl namespace eventually
ConsumptionProfileMap 		g_consumptionProfiles;

namespace PintoolControl {
	GeneralBuffers generalBuffers;

	#if PIN_PRIVATE_LOCKED 
		ThreadControlMap threadControlMap;
	#else
		ThreadControl g_mainThreadControl(-1);
	#endif

	//i had to add the next two because i needed a simple and direct way of enabling and disabling the error injection
	VOID enable_global_injection(IF_PIN_PRIVATE_LOCKED(const THREADID threadId)) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		tdata.m_injectionEnabled = true;

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	VOID disable_global_injection(IF_PIN_PRIVATE_LOCKED(const THREADID threadId)) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		tdata.m_injectionEnabled = false;

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	VOID disable_access_instrumentation(IF_PIN_PRIVATE_LOCKED(const THREADID threadId)) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		SET_ACCESS_INSTRUMENTATION_STATUS(false)

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	//effectively enables the error injection  //not a boolean to allow layers (so functions that call each other don't disable the injection)
	VOID start_level(IF_PIN_PRIVATE_LOCKED(const THREADID threadId)) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		tdata.m_level++;

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	//effectively disables the error injection
	VOID end_level(IF_PIN_PRIVATE_LOCKED(const THREADID threadId)) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		tdata.m_level--;

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	VOID next_period(IF_PIN_PRIVATE_LOCKED(const THREADID threadId)) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		++g_currentPeriod;

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		#if MULTIPLE_ACTIVE_BUFFERS
			for (const auto& [_, activeBuffer] : tdata.m_activeBuffers) {
				activeBuffer->NextPeriod(g_currentPeriod);
			}
		#else
			if (tdata.m_activeBuffer != nullptr) {
				tdata.m_activeBuffer->NextPeriod(g_currentPeriod);
			}
		#endif

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	VOID add_approx(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const int64_t bufferId, const int64_t configurationId, const uint32_t dataSizeInBytes) {
		const Range range = Range(start_address, end_address);

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		IF_PIN_ANY_LOCKED(PIN_GetLock(&g_pinLock, -1);) //note: needed for generalBuffers

		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator lbActive = tdata.m_activeBuffers.lower_bound(range);
			if (!((lbActive != tdata.m_activeBuffers.cend()) && !(tdata.m_activeBuffers.key_comp()(range, lbActive->first)))) //only inserts if it wasn't found (done like this to avoid possible memory leaks from the new's in case there's a overlap)
		#else
			if (tdata.m_activeBuffer == nullptr)
		#endif
		{
			const GeneralBufferRecord generalBufferKey = std::make_tuple(range.m_initialAddress, range.m_finalAddress, bufferId, configurationId, dataSizeInBytes);
			const GeneralBuffers::const_iterator lbGeneral = PintoolControl::generalBuffers.lower_bound(generalBufferKey);

			if ((lbGeneral != PintoolControl::generalBuffers.cend()) && !(PintoolControl::generalBuffers.key_comp()(generalBufferKey, lbGeneral->first))) {
				#if MULTIPLE_ACTIVE_BUFFERS
					ChosenTermApproximateBuffer* const approxBuffer = lbGeneral->second.get();
					approxBuffer->ReactivateBuffer(g_currentPeriod);
					tdata.m_activeBuffers.insert(lbActive, {range, approxBuffer});
				#else
					tdata.m_activeBuffer = lbGeneral->second.get();
					tdata.m_activeBuffer->ReactivateBuffer(g_currentPeriod);
				#endif
			} else {
				const InjectorConfigurationMap::const_iterator bcIt = g_injectorConfigurations.find(configurationId);

				if (bcIt == g_injectorConfigurations.cend()) {
					std::cerr << "ApproxSS Error: Configuration " << configurationId << " not found." << std::endl;
					PIN_ExitProcess(EXIT_FAILURE);
				}

				ChosenTermApproximateBuffer* const approxBuffer = new ChosenTermApproximateBuffer(range, bufferId, g_currentPeriod, dataSizeInBytes, *bcIt->second);

				#if MULTIPLE_ACTIVE_BUFFERS
					tdata.m_activeBuffers.insert(lbActive, {range, approxBuffer});
				#else
					tdata.m_activeBuffer = approxBuffer;
				#endif

				PintoolControl::generalBuffers.emplace_hint(lbGeneral, generalBufferKey, std::unique_ptr<ChosenTermApproximateBuffer>(approxBuffer)); //TODO: LOCK PRA CA
			}
		} else {
			std::cout << "ApproxSS Warning: approximate buffer (id: " << bufferId << ") already active. Ignoring addition request." << std::endl;
		}

		IF_PIN_ANY_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	VOID remove_approx(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const bool giveAwayRecords) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		const Range range = Range(start_address, end_address);

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator lbActive = tdata.m_activeBuffers.find(range); 
			if (lbActive != tdata.m_activeBuffers.cend() && lbActive->first.IsEqual(range)){
				lbActive->second->RetireBuffer(giveAwayRecords);
				tdata.m_activeBuffers.erase(lbActive);
			}
		#else
			if (tdata.m_activeBuffer != nullptr && tdata.m_activeBuffer->IsEqual(range)) {
				tdata.m_activeBuffer->RetireBuffer(giveAwayRecords);
				tdata.m_activeBuffer = nullptr;
			}
		#endif
		 else {
			std::cout << "ApproxSS Warning: approximate buffer not found for removal. Ignorning request." << std::endl;
		}

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	#if PIN_ANY_LOCKED
		#if PIN_PRIVATE_LOCKED
			static PIN_LOCK tcMap_lock;
		#endif
			
		VOID ThreadStart(const THREADID threadId, CONTEXT * ctxt, const INT32 flags, VOID * v) {
			std::cout << std::endl << "Thread STARTED. Id: " << threadId  << std::endl;

			#if PIN_PRIVATE_LOCKED
				PIN_GetLock(&tcMap_lock, threadId); //note: pretty sure this is unnecessary, but why not?
				const std::pair<const ThreadControlMap::const_iterator, const bool> it = PintoolControl::threadControlMap.insert({threadId, std::make_unique<ThreadControl>(threadId)});
				PIN_ReleaseLock(&tcMap_lock);

				if (PIN_SetThreadData(g_tlsKey, it.first->second.get(), threadId) == FALSE) {
					std::cerr << "Pin Error: PIN_SetThreadData failed" << std::endl;
					PIN_ExitProcess(EXIT_FAILURE);
				}
			#endif
		}
		
		// This function is called when the thread exits
		VOID ThreadFini(const THREADID threadId, CONTEXT const * const ctxt, const INT32 code, VOID * v) {
			std::cout << std::endl << "Thread ENDED: " << threadId << std::endl;

			#if PIN_PRIVATE_LOCKED
				PIN_GetLock(&tcMap_lock, threadId); //note: pretty sure this is unnecessary, but why not?
				PintoolControl::threadControlMap.erase(threadId);
				PIN_ReleaseLock(&tcMap_lock);
			#endif
		}
	#endif
}

/* ====================================================================	*/
/* Inspect each memory read and write									*/
/* ==================================================================== */

namespace AccessHandler {
	VOID CheckAndForward(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) void (ChosenTermApproximateBuffer::*function)(uint8_t* const, const UINT32, const bool), uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator approxBuffer =  tdata.m_activeBuffers.find(Range{accessedAddress, accessedAddress});
			if (approxBuffer != tdata.m_activeBuffers.cend()) {
				(*(approxBuffer->second).*function)(accessedAddress, accessSizeInBytes, tdata.isThreadInjectionEnabled());
			}
		#else
			if (tdata.m_activeBuffer != nullptr && tdata.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
				(*(tdata.m_activeBuffer).*function)(accessedAddress, accessSizeInBytes, tdata.isThreadInjectionEnabled());
			}
		#endif

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	// memory read
	VOID HandleMemoryReadSIMD(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {		
		CheckAndForward(IF_PIN_PRIVATE_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSIMD, accessedAddress, accessSizeInBytes);
	}

	VOID HandleMemoryRead(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {		
		CheckAndForward(IF_PIN_PRIVATE_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSingleElementSafe, accessedAddress, accessSizeInBytes);
	}

	// memory write
	VOID HandleMemoryWriteSIMD(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
		CheckAndForward(IF_PIN_PRIVATE_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteSIMD, accessedAddress, accessSizeInBytes);
	}

	VOID HandleMemoryWrite(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
		CheckAndForward(IF_PIN_PRIVATE_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteSingleElementSafe, accessedAddress, accessSizeInBytes);
	}

	VOID CheckAndForwardScattered(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) void (ChosenTermApproximateBuffer::*function)(IMULTI_ELEMENT_OPERAND const * const, const bool), IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		if (memOpInfo->NumOfElements() < 1) {
			return;
		}
		
		uint8_t * accessedAddress = (uint8_t*) memOpInfo->ElementAddress(0); 

		IF_PIN_SHARED_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		#if PIN_PRIVATE_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif
		
		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator it = tdata.m_activeBuffers.find(Range{accessedAddress, accessedAddress});
			if (it != tdata.m_activeBuffers.cend()) {
				ChosenTermApproximateBuffer * const approxBuffer = it->second;
				(*approxBuffer.*function)(memOpInfo, tdata.isThreadInjectionEnabled());		
			}
		#else
			if (tdata.m_activeBuffer != nullptr && tdata.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
				ChosenTermApproximateBuffer * const approxBuffer = tdata.m_activeBuffer;
				(*approxBuffer.*function)(memOpInfo, tdata.isThreadInjectionEnabled());		
			}
		#endif

		IF_PIN_SHARED_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	VOID HandleMemoryReadScattered(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		CheckAndForwardScattered(IF_PIN_PRIVATE_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadScattered, memOpInfo);
	}

	VOID HandleMemoryWriteScattered(IF_PIN_PRIVATE_LOCKED_COMMA(const THREADID threadId) IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		CheckAndForwardScattered(IF_PIN_PRIVATE_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteScattered, memOpInfo);
	}
}

// This function is called before every instruction is executed
namespace TargetInstrumentation {
	// Is called for every instruction and instruments reads and writes
	VOID Instruction(const INS ins, VOID* v) {
		// Instruments memory accesses using a predicated call, i.e.
		// the instrumentation is called if the instruction will actually be executed.
		//
		// On the IA-32 and Intel(R) 64 architectures conditional moves and REP 
		// prefixed instructions appear as predicated instructions in Pin.

		ASSERT_ACCESS_INSTRUMENTATION_ACTIVE()

		const UINT32 memOperands = INS_MemoryOperandCount(ins);
		// Iterate over each memory operand of the instruction.
		for (UINT32 memOp = 0; memOp < memOperands; ++memOp) {		
			if (INS_MemoryOperandIsRead(ins, memOp)) {
				if (!INS_HasScatteredMemoryAccess(ins)) {
					if (INS_MemoryOperandElementCount(ins, memOp) > 1) {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadSIMD, IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryRead, IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadScattered, IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
						IARG_MULTI_ELEMENT_OPERAND, op,
						IARG_END);
				}
			}
			// Note that in some architectures a single memory operand can be 
			// both read and written (for instance incl (%eax) on IA-32)
			// In that case we instrument it once for read and once for write.

			if (INS_MemoryOperandIsWritten(ins, memOp)) {
				if (!INS_HasScatteredMemoryAccess(ins)) {
					if (INS_MemoryOperandElementCount(ins, memOp) > 1) {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteSIMD, IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYWRITE_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWrite, IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYWRITE_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteScattered, IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
						IARG_MULTI_ELEMENT_OPERAND, op,
						IARG_END);
				}
			}
		}
	}

	/* ===================================================================== */
	/* Register functions to track										   */
	/* ===================================================================== */

	VOID Routine(const RTN rtn, VOID* v) {
		const std::string rtnName = RTN_Name(rtn);

		/*by my experience, if more than one of these functions have the same number of parameters, 
		they'll end up calling each other. the actual function in the pintool doesn't appear to need the parameters, 
		but having them in the instrumentalized code is advised, tho i don't really know if necessary*/

		// Insert a call at the entry point of routines
		if (rtnName.find("start_level") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::start_level,  
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("end_level") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::end_level,  
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID) IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("next_period") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::next_period, 
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0, 
							IARG_FUNCARG_ENTRYPOINT_VALUE, 1, 
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("add_approx") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::add_approx, 
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0, 
							IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 4, 
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("remove_approx") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::remove_approx,  
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0, 
							IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 2, 
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("enable_global_injection") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::enable_global_injection, 
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0, 
							IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 3, 
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("disable_global_injection") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::disable_global_injection,  
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0, 
							IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 4,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 5, 
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("disable_access_instrumentation") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::disable_access_instrumentation,  
							IF_PIN_PRIVATE_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0, 
							IARG_FUNCARG_ENTRYPOINT_VALUE, 1,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 2,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 3,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 4,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 5,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 6, 
							IARG_END);
			RTN_Close(rtn);
			return;
		}

		if (rtnName.find("enable_access_instrumentation") != std::string::npos) {
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}
	}

}

namespace PintoolOutput {
	std::ofstream accessLog;
	std::ofstream energyConsumptionLog;

	void PrintEnabledOrDisabled(const char* const message, const bool enabled) {
		std::cout << "\t" << message << ": ";
		if (enabled) {
			std::cout << "Enabled";
		} else {
			std::cout << "Disabled";
		}
		std::cout << std::endl;
	}

	void PrintPintoolConfiguration() {
		std::cout << std::string(50, '#') << std::endl;

		std::cout << "PINTOOL CONFIGURATIONS:" << std::endl;
		std::cout << "\tFault Injector: " <<
		#if DISTANCE_BASED_FAULT_INJECTOR
			"Distance-Based"
		#elif GRANULAR_FAULT_INJECTOR
			"Granular"
		#else
			"Default"
		#endif
		<< std::endl;

		std::cout << "\tApproximate Buffer Term: " <<
		#if SHORT_TERM_BUFFER
			"Short"
		#else
			"Long"
		#endif
		<< std::endl;

		PintoolOutput::PrintEnabledOrDisabled("Passive Injection", ENABLE_PASSIVE_INJECTION);
		PintoolOutput::PrintEnabledOrDisabled("Multiple Active Buffers", MULTIPLE_ACTIVE_BUFFERS);
		PintoolOutput::PrintEnabledOrDisabled("Multiple BER Configuration", MULTIPLE_BER_CONFIGURATION);
		PintoolOutput::PrintEnabledOrDisabled("Multiple BER Element", MULTIPLE_BER_ELEMENT);
		PintoolOutput::PrintEnabledOrDisabled("Fault Logging", LOG_FAULTS);
		PintoolOutput::PrintEnabledOrDisabled("Narrow Access Instrumentation", NARROW_ACCESS_INSTRUMENTATION);
		PintoolOutput::PrintEnabledOrDisabled("Overcharge BERs", OVERCHARGE_FLIP_BACK);
		PintoolOutput::PrintEnabledOrDisabled("Overcharge flip-back", OVERCHARGE_FLIP_BACK);
		PintoolOutput::PrintEnabledOrDisabled("Least significant bits dropping", LS_BIT_DROPPING);
		PintoolOutput::PrintEnabledOrDisabled("Multithreading global shared mutex", PIN_SHARED_LOCKED);
		PintoolOutput::PrintEnabledOrDisabled("Multithreading private mutex", PIN_PRIVATE_LOCKED);


		std::cout << std::string(50, '#') << std::endl;
	}

	void DeleteDataEstructures() {
		PintoolControl::generalBuffers.clear();

		g_injectorConfigurations.clear();
	}

	std::string GenerateTimeDependentFileName(const std::string& suffix) {
		const std::time_t currentTime = std::time(nullptr);
		std::stringstream outputFilenameStream;
		outputFilenameStream << "injectionPintoolOutputLog_" << std::put_time(std::localtime(&currentTime), "%Y:%m:%d:%H:%M:%S") << '_' << suffix;
		return outputFilenameStream.str();
	}

	void CreateOutputLog(std::ofstream& outputFile, std::string outputFilename, const std::string& suffix) {
		if (outputFilename.empty()) {
			outputFilename = PintoolOutput::GenerateTimeDependentFileName(suffix);
		}
		
		outputFile.open(outputFilename, std::ofstream::trunc);

		if (!outputFile) {
			std::cerr << "ApproxSS Error: Unable to create output file: \"" << outputFilename + "\"." << std::endl;
			PIN_ExitProcess(EXIT_FAILURE);
		}
	}

	VOID WriteAccessLog() {
		//PintoolOutput::accessLog << "Final Level: " << g_level << std::endl;
		PintoolOutput::accessLog << "Total Injection Calls: " << g_injectionCalls << std::endl;
		
		std::array<uint64_t, ErrorCategory::Size> totalTargetInjections;
		std::fill_n(totalTargetInjections.data(), ErrorCategory::Size, 0);

		std::array<uint64_t, AccessTypes::Size> totalTargetAccessesBytes;
		std::fill_n(totalTargetAccessesBytes.data(), AccessTypes::Size, 0);

		for (const auto& [_, approxBuffer] : PintoolControl::generalBuffers) { 
			approxBuffer->WriteAccessLogToFile(PintoolOutput::accessLog, totalTargetAccessesBytes, totalTargetInjections);
		}

		uint64_t totalAccesses = 0;
		PintoolOutput::accessLog << std::endl;
		for (size_t i = 0; i < AccessTypes::Size; ++i) {
			PintoolOutput::accessLog << "Total Software Implementation " << AccessTypesNames[i] << " Bytes/Bits: " << totalTargetAccessesBytes[i] << " / " << (totalTargetAccessesBytes[i] * BYTE_SIZE) << std::endl;
			totalAccesses += totalTargetAccessesBytes[i];
		}
		PintoolOutput::accessLog << "Total Software Implementation Accessed Bytes/Bits: " << totalAccesses << " / " << (totalAccesses * BYTE_SIZE) << std::endl;

		#if LOG_FAULTS
			uint64_t totalInjections = 0;
			PintoolOutput::accessLog << std::endl;

			for (size_t i = 0; i < ErrorCategory::Size; ++i) {
				std::string errorCat = ErrorCategoryNames[i];
				//StringHandling::toLower(errorCat);

				PintoolOutput::accessLog << "Total " << errorCat << " Errors Injected: " << totalTargetInjections[i] << std::endl;
				totalInjections += totalTargetInjections[i];
			}

			PintoolOutput::accessLog << "Total Errors Injected: " << (totalInjections) << std::endl;
		#endif
		
		PintoolOutput::accessLog.close();
	}

	VOID WriteEnergyLog() {
		std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> totalTargetEnergy;
		std::fill_n(totalTargetEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, 0);

		PintoolOutput::energyConsumptionLog.setf(std::ios::fixed);
		PintoolOutput::energyConsumptionLog.precision(2);

		for (const auto& [_, approxBuffer] : PintoolControl::generalBuffers) { 
			const int64_t configurationId = approxBuffer->GetConfigurationId();
			const ConsumptionProfileMap::const_iterator profileIt = g_consumptionProfiles.find(configurationId);

			if (profileIt == g_consumptionProfiles.cend()) {
				std::cerr << "ApproxSS Error: somehow, Consumption Profile not informed." << std::endl;
				PIN_ExitProcess(EXIT_FAILURE);
			}

			const ConsumptionProfile& respectiveConsumptionProfile = *(profileIt->second.get());

			approxBuffer->WriteEnergyLogToFile(PintoolOutput::energyConsumptionLog, totalTargetEnergy, respectiveConsumptionProfile);
		}

		PintoolOutput::energyConsumptionLog << std::endl << "TARGET APPLICATION TOTAL ENERGY CONSUMPTION" << std::endl;
		WriteEnergyConsumptionToLogFile(PintoolOutput::energyConsumptionLog, totalTargetEnergy, false, false, "	");
		WriteEnergyConsumptionSavingsToLogFile(PintoolOutput::energyConsumptionLog, totalTargetEnergy, false, false, "	");

		PintoolOutput::accessLog.close();
	}

	VOID Fini(const INT32 code, VOID* v) {
		#if PIN_PRIVATE_LOCKED
			for (const auto& [_, tdata] : PintoolControl::threadControlMap) {
				tdata->~ThreadControl();
			}
		#else
			PintoolControl::g_mainThreadControl.~ThreadControl();
		#endif

		PintoolOutput::WriteAccessLog();

		if (!g_consumptionProfiles.empty()) {
			PintoolOutput::WriteEnergyLog();
		}

		PintoolOutput::DeleteDataEstructures();
	}
}

/* ==================================================================== */
/* Print Help Message													*/
/* ==================================================================== */
   
INT32 Usage() {
	PIN_ERROR("This ApproxSS injects memory faults at addresses registered by calling add_approx()\n" 
			  + KNOB_BASE::StringKnobSummary() + "\n");
	return -1;
}

/* ==================================================================== */
/* Commandline Switches 												*/
/* ==================================================================== */

KNOB<std::string> InjectorConfigurationFile(KNOB_MODE_WRITEONCE, "pintool", "cfg", "", "specify the error injector configuration file");
KNOB<std::string> EnergyProfileFile(KNOB_MODE_WRITEONCE, "pintool", "pfl", "", "specify the energy consumption profile");
KNOB<std::string> AccessOutputFile(KNOB_MODE_WRITEONCE, "pintool", "aof", "", "specify the memory access output log");
KNOB<std::string> EnergyConsumptionOutputFile(KNOB_MODE_WRITEONCE, "pintool", "cof", "", "specify the energy consumpion output log");

/* ==================================================================== */
/* Main																	*/
/* ==================================================================== */

int main(const int argc, char* argv[]) {
	srand((unsigned)getpid() * (unsigned)time(0));   

	// Initialize symbol table code, needed for rtn instrumentation
	PIN_InitSymbols();

	if (PIN_Init(argc, argv)) return Usage();

	PintoolOutput::PrintPintoolConfiguration();
	PintoolInput::ProcessInjectorConfiguration(InjectorConfigurationFile.Value());
	PintoolOutput::CreateOutputLog(PintoolOutput::accessLog, AccessOutputFile.Value(), "access.log");

	PintoolInput::ProcessEnergyProfile(EnergyProfileFile.Value());

	if (!g_consumptionProfiles.empty()) {
		PintoolOutput::CreateOutputLog(PintoolOutput::energyConsumptionLog, EnergyConsumptionOutputFile.Value(), "energyConsumpion.log");
	}

	// Register Routine to be called to instrument rtn
	RTN_AddInstrumentFunction(TargetInstrumentation::Routine, nullptr);

	// Obtain  a key for TLS storage.
	g_tlsKey = PIN_CreateThreadDataKey(nullptr);
    if (g_tlsKey == INVALID_TLS_KEY)    {
        std::cerr << "Pin Error: number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << std::endl;
        PIN_ExitProcess(EXIT_FAILURE);
    }
	
	#if PIN_ANY_LOCKED
		// Register ThreadStart to be called when a thread starts.
		PIN_AddThreadStartFunction(PintoolControl::ThreadStart, nullptr);
	
		// Register Fini to be called when thread exits.
		PIN_AddThreadFiniFunction(PintoolControl::ThreadFini, nullptr);
	#endif

	INS_AddInstrumentFunction(TargetInstrumentation::Instruction, nullptr);

	PIN_AddFiniFunction(PintoolOutput::Fini, nullptr);

	// Never returns
	PIN_StartProgram();
	
	return 0;
}
