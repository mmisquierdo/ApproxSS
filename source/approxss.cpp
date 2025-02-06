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
uint64_t g_injectionCalls 	= 0; //NOTE: possible race condition, but I don't care

uint64_t g_currentPeriod 	= 999; //starting POC of a random_access encoding //NOTE: possible minor race condition, but 99.9999% inconsequential and also actually impossible in current lock implementation

uint64_t g_fullAccessCount = 0;

std::vector<int64_t> levels;

#if PIN_LOCKED
	PIN_LOCK g_pinLock;
	TLS_KEY g_tlsKey = INVALID_TLS_KEY;
#endif

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
			return lhv.m_finalAddress < rhv.m_initialAddress;
		} 
	};
	typedef std::map<Range, ChosenTermApproximateBuffer*, RangeCompare> ActiveBuffers;
#endif

int64_t array_hash(const std::vector<int64_t>& sequence) {
    int64_t result = 0;

    for (const auto& value : sequence) {

        result ^= value + 0x9E3779B97F4A7C15 + (result << 6) + (result >> 2);
    }

    return result;
}

HashedSequence g_hashedSequence;
int64_t g_sequenceHash = 0;

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
		//return this->m_level && m_injectionEnabled;
		return true;
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

	bool HasActiveBuffer() const {
		return 
		#if MULTIPLE_ACTIVE_BUFFERS
			(!this->m_activeBuffers.empty())
		#else
			(this->m_activeBuffer)
		#endif
		;
	}

	size_t HowManyActiveBuffers() const {
		return 
		#if MULTIPLE_ACTIVE_BUFFERS
			(this->m_activeBuffers.size())
		#else
			(this->m_activeBuffer)
		#endif
		;
	}

	bool IsPresent(const Range& range) const {
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

	void PrintStillActiveBuffers() {
		std::cout << "Still active buffer(s): ";
		std::cout << "\tIds: ";
		for (auto const& [range, buffer] : this->m_activeBuffers) {
			std::cout << buffer->GetBufferId() << ';';
		}
		std::cout << std::endl;
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
	ThreadControl g_mainThreadControl(-1);

	#if PIN_LOCKED 
		ThreadControlMap threadControlMap;	
	#endif

	//i had to add the next two because i needed a simple and direct way of enabling and disabling the error injection
	VOID enable_global_injection(IF_PIN_LOCKED(const THREADID threadId)) {
		#if PIN_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId))); //TODO: they only really make sense for their thread 
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

	//effectively enables the error injection  //not a boolean to allow layers (so functions that call each other don't disable the injection)
	VOID start_level(IF_PIN_LOCKED_COMMA(const THREADID threadId) const int64_t level) {
		//std::cout << "<start_level>" << std::endl;
		#if PIN_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		tdata.m_level++;

		levels.push_back(level);

		/*std::cout << "Levels: " << levels.size() << " - ";
		for (const auto& l : levels) {
			std::cout << " " << l << ";";
		} 
		std::cout << array_hash(levels) << std::endl;*/

		g_sequenceHash = array_hash(levels);

		g_hashedSequence.emplace(g_sequenceHash, levels);

		//std::cout << "</start_level>" << std::endl;
	}

	//effectively disables the error injection
	VOID end_level(IF_PIN_LOCKED(const THREADID threadId)) {
		//std::cout << "<end_level>" << std::endl;
		#if PIN_LOCKED
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			ThreadControl& tdata = PintoolControl::g_mainThreadControl;
		#endif

		tdata.m_level--;

		levels.pop_back();

		g_sequenceHash = array_hash(levels);
		//std::cout << "</end_level>" << std::endl;
	}

	VOID next_period(const uint64_t period) {
		IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		//++g_currentPeriod;
		g_currentPeriod = period;

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

	VOID add_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const int64_t bufferId, const int64_t configurationId, const uint32_t dataSizeInBytes) {
		//std::cout << "<add_approx>" << std::endl;

		const Range range = Range(start_address, end_address-1);
		
		ThreadControl& mainThread = PintoolControl::g_mainThreadControl;

		IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		#if MULTIPLE_ACTIVE_BUFFERS
			ActiveBuffers::const_iterator lbActiveMain = mainThread.m_activeBuffers.lower_bound(range);
			if (!((lbActiveMain != mainThread.m_activeBuffers.cend()) && !(mainThread.m_activeBuffers.key_comp()(range, lbActiveMain->first)))) //only inserts if it wasn't found (done like this to avoid possible memory leaks from the new's in case there's a overlap)
		#else
			if (mainThread.m_activeBuffer == nullptr)
		#endif
		{
			const GeneralBufferRecord generalBufferKey = std::make_tuple(range.m_initialAddress, range.m_finalAddress, bufferId, configurationId, dataSizeInBytes);
			const GeneralBuffers::const_iterator lbGeneral = PintoolControl::generalBuffers.lower_bound(generalBufferKey);

			if ((lbGeneral != PintoolControl::generalBuffers.cend()) && !(PintoolControl::generalBuffers.key_comp()(generalBufferKey, lbGeneral->first))) {
				#if MULTIPLE_ACTIVE_BUFFERS
					ChosenTermApproximateBuffer* const approxBuffer = lbGeneral->second.get();
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

				ChosenTermApproximateBuffer* const approxBuffer = new ChosenTermApproximateBuffer(range, bufferId, g_currentPeriod, dataSizeInBytes, *bcIt->second);

				#if MULTIPLE_ACTIVE_BUFFERS
					lbActiveMain = mainThread.m_activeBuffers.insert(lbActiveMain, {range, approxBuffer});
				#else
					mainThread.m_activeBuffer = approxBuffer;
				#endif

				PintoolControl::generalBuffers.emplace_hint(lbGeneral, generalBufferKey, std::unique_ptr<ChosenTermApproximateBuffer>(approxBuffer));
			}
		} 
		#if !PIN_LOCKED
			else {
				std::cout << "ApproxSS Warning: approximate buffer (id: " << bufferId << " (" << (size_t) start_address << "-" << (size_t) end_address << ")) already active (as id: " << lbActiveMain->second->GetBufferId() << " (" << (size_t) lbActiveMain->second->m_initialAddress << "-" << (size_t) lbActiveMain->second->m_finalAddress << ")). Ignoring addition request." << std::endl;
			}
		#endif

		{
			#if PIN_LOCKED 
				ThreadControl& localThread = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));

				#if MULTIPLE_ACTIVE_BUFFERS
					const ActiveBuffers::const_iterator lbActiveLocal = localThread.m_activeBuffers.lower_bound(range);
					if (!((lbActiveLocal != localThread.m_activeBuffers.cend()) && !(localThread.m_activeBuffers.key_comp()(range, lbActiveLocal->first)))) { //only inserts if it wasn't found (done like this to avoid possible memory leaks from the new's in case there's a overlap)
						ChosenTermApproximateBuffer* const approxBuffer = lbActiveMain->second;
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

		//std::cout << "</add_approx>" << std::endl;
	}

	VOID remove_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const bool giveAwayRecords) {
		//std::cout << "<remove_approx>" << std::endl;

		const Range range = Range(start_address, end_address-1);
		ThreadControl& mainThread = PintoolControl::g_mainThreadControl;	

		IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		{
		#if PIN_LOCKED
			ThreadControl& localThread = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId))); //TODO: remove approx buffer from both maps

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
			  else {/*
				bool isPresent = false;
				for (auto const& [generalKey, buffer] : PintoolControl::generalBuffers) {
					if (buffer->IsEqual(range)) {
						isPresent = true;
						break;
					}
				}

				if (isPresent) {
					std::cout << "ApproxSS Warning: approximate buffer not found for removal but present in past records. Ignorning request." << std::endl;
				} else {
					std::cout << "ApproxSS Warning: approximate buffer not found for removal nor in past records. Ignorning request." << std::endl;
				}*/
			}
		#endif

		IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)

		//std::cout << "<remove_approx>" << std::endl;
	}

	#if PIN_LOCKED
		static PIN_LOCK tcMap_lock;
			
		VOID ThreadStart(const THREADID threadId, CONTEXT * ctxt, const INT32 flags, VOID * v) {
			std::cout << std::endl << "Target application thread STARTED. Id: " << threadId  << std::endl;

			PIN_GetLock(&tcMap_lock, threadId); //note: pretty sure this is unnecessary, but why not?
			const std::pair<const ThreadControlMap::const_iterator, const bool> it = PintoolControl::threadControlMap.insert({threadId, std::make_unique<ThreadControl>(threadId)});
			PIN_ReleaseLock(&tcMap_lock);

			if (PIN_SetThreadData(g_tlsKey, it.first->second.get(), threadId) == FALSE) {
				std::cerr << "Pin Error: PIN_SetThreadData failed" << std::endl;
				PIN_ExitProcess(EXIT_FAILURE);
			}
		}
		
		// This function is called when the thread exits
		VOID ThreadFini(const THREADID threadId, CONTEXT const * const ctxt, const INT32 code, VOID * v) {
			ThreadControl& tdata = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));

			std::cout << std::endl << "Target application thread ENDED: " << threadId << ". Final level: " << tdata.m_level << std::endl;

			PIN_GetLock(&tcMap_lock, threadId); //note: pretty sure this is unnecessary, but why not?
			PintoolControl::threadControlMap.erase(threadId);
			PIN_ReleaseLock(&tcMap_lock);
		}
	#endif
}

/* ====================================================================	*/
/* Inspect each memory read and write									*/
/* ==================================================================== */

namespace AccessHandler {
	/*static bool ShouldInject(IF_PIN_LOCKED_COMMA(const THREADID threadId) IF_PIN_LOCKED(const Range& range)) {
		#if PIN_LOCKED
			const ThreadControl& localThread = *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
			return localThread.HasActiveBuffer() && localThread.isThreadInjectionEnabled() && localThread.IsPresent(range);
		#else
			const ThreadControl& mainThread = PintoolControl::g_mainThreadControl;
			return mainThread.isThreadInjectionEnabled();
		#endif
	}*/
	/*static const ThreadControl& GetInterestThreadControl(IF_PIN_LOCKED(const THREADID threadId)) {
		#if PIN_LOCKED
			return *(static_cast<ThreadControl*>(PIN_GetThreadData(g_tlsKey, threadId)));
		#else
			return PintoolControl::g_mainThreadControl;
		#endif
	}*/

	/*static bool IsPresent(IF_PIN_LOCKED_COMMA(const ThreadControl& threadControl) IF_PIN_LOCKED(const Range& range)) {
		#if PIN_LOCKED
			return threadControl.IsPresent(range);
		#else
			return true;
		#endif
	}*/

	VOID CheckAndForward(/*IF_PIN_LOCKED_COMMA(const THREADID threadId) void (ChosenTermApproximateBuffer::*function)(uint8_t* const, const UINT32, const bool IF_COMMA_PIN_LOCKED(const bool)), uint8_t* const accessedAddress,*/ const UINT32 accessSizeInBytes) {
		//std::cout << "<CheckAndForward>" << std::endl;
		#if PIN_LOCKED
			if (!PintoolControl::g_mainThreadControl.HasActiveBuffer())	{
				return;
			}
		#endif

		g_fullAccessCount += accessSizeInBytes;

		IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)

		/*const ThreadControl& mainThread = PintoolControl::g_mainThreadControl;

		#if MULTIPLE_ACTIVE_BUFFERS || PIN_LOCKED
			const Range range = Range(accessedAddress, accessedAddress);
		#endif

		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator it =  mainThread.m_activeBuffers.find(range);
			if (it != mainThread.m_activeBuffers.cend()) {
				ChosenTermApproximateBuffer& approxBuffer = *(it->second);
				const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
				(approxBuffer.*function)(accessedAddress, accessSizeInBytes, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
			}
		#else
			if (mainThread.m_activeBuffer != nullptr && mainThread.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
				ChosenTermApproximateBuffer& approxBuffer = *(mainThread.m_activeBuffer);
				const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
				(approxBuffer.*function)(accessedAddress, accessSizeInBytes, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
			}
		#endif*/

		IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
		//std::cout << "</CheckAndForward>" << std::endl;
	}

	// memory read
	VOID HandleMemoryReadSIMD(/*IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress,*/ const UINT32 accessSizeInBytes) {		
		CheckAndForward(/*IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSIMD, accessedAddress,*/ accessSizeInBytes);
	}

	VOID HandleMemoryRead(/*IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress,*/ const UINT32 accessSizeInBytes) {		
		CheckAndForward(/*IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSingleElementSafe, accessedAddress,*/ accessSizeInBytes);
	}

	// memory write
	VOID HandleMemoryWriteSIMD(/*IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress,*/ const UINT32 accessSizeInBytes) {
		CheckAndForward(/*IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteSIMD, accessedAddress,*/ accessSizeInBytes);
	}

	VOID HandleMemoryWrite(/*IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress,*/ const UINT32 accessSizeInBytes) {
		CheckAndForward(/*IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteSingleElementSafe, accessedAddress,*/ accessSizeInBytes);
	}

	VOID CheckAndForwardScattered(/*IF_PIN_LOCKED_COMMA(const THREADID threadId) void (ChosenTermApproximateBuffer::*function)(IMULTI_ELEMENT_OPERAND const * const, const bool IF_COMMA_PIN_LOCKED(const bool)),*/ IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		#if PIN_LOCKED
			if (!PintoolControl::g_mainThreadControl.HasActiveBuffer())	{
				return;
			}
		#endif

		if (memOpInfo->NumOfElements() < 1) {
			return;
		}

		//std::cout << "<CheckAndForwardScattered>" << std::endl;

		g_fullAccessCount += memOpInfo->NumOfElements() * memOpInfo->ElementSize(0);
		
		/*uint8_t * accessedAddress = (uint8_t*) memOpInfo->ElementAddress(0); 
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
			}
		#else
			if (mainThread.m_activeBuffer != nullptr && mainThread.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
				ChosenTermApproximateBuffer& approxBuffer = *(mainThread.m_activeBuffer);

				const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));

				(approxBuffer.*function)(memOpInfo, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
			}
		#endif*/

		IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)

		//std::cout << "</CheckAndForwardScattered>" << std::endl;
	}

	VOID HandleMemoryReadScattered(/*IF_PIN_LOCKED_COMMA(const THREADID threadId)*/ IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		CheckAndForwardScattered(/*IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadScattered,*/ memOpInfo);
	}

	VOID HandleMemoryWriteScattered(/*IF_PIN_LOCKED_COMMA(const THREADID threadId)*/ IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		CheckAndForwardScattered(/*IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryWriteScattered,*/ memOpInfo);
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
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadSIMD,/* IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp,*/ IARG_MEMORYREAD_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryRead, /*IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp,*/ IARG_MEMORYREAD_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadScattered, //IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteSIMD, /*IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp,*/ IARG_MEMORYWRITE_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWrite, /*IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp,*/ IARG_MEMORYWRITE_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteScattered, //IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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
							IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("end_level") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::end_level,  
							IF_PIN_LOCKED_COMMA(IARG_THREAD_ID) IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("next_period") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::next_period,
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
							IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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
							IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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
							IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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
							IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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

		#if NARROW_ACCESS_INSTRUMENTATION
			if (rtnName.find("disable_access_instrumentation") != std::string::npos) {
				RTN_Open(rtn);
				RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::disable_access_instrumentation,  
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
		#endif
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
		PintoolOutput::PrintEnabledOrDisabled("Multithreading support: shared buffer list, thread-level control", PIN_LOCKED);

		std::cout << std::string(50, '#') << std::endl;
	}

	void DeleteDataEstructures() { 
		#if PIN_LOCKED
			PintoolControl::threadControlMap.clear();
		#endif

		//std::cout << "Clearing " << PintoolControl::generalBuffers.size() << " General Buffers..." << std::endl;
		PintoolControl::generalBuffers.clear();

		//std::cout << "Clearing Injector Configuraitons..." << std::endl;
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
		PintoolOutput::accessLog << "Total Injection Calls: " << g_injectionCalls << std::endl;
		
		std::array<uint64_t, ErrorCategory::Size> totalTargetInjections;
		std::fill_n(totalTargetInjections.data(), ErrorCategory::Size, 0);

		std::array<uint64_t, AccessTypes::Size> totalTargetAccessesBytes;
		std::fill_n(&(totalTargetAccessesBytes[0]), AccessTypes::Size, 0);

		for (const auto& [_, approxBuffer] : PintoolControl::generalBuffers) { 
			approxBuffer->WriteAccessLogToFile(PintoolOutput::accessLog, totalTargetAccessesBytes, totalTargetInjections);
		}

		uint64_t totalAccesses = 0;
		PintoolOutput::accessLog << std::endl;
		//for (size_t i = 0; i < AccessPrecision::Size; ++i) {
			for (size_t j = 0; j < AccessTypes::Size; ++j) {
				PintoolOutput::accessLog << "Total Software Implementation " /*<< AccessPrecisionNames[i] << " "*/ << AccessTypesNames[j] << " Bytes: " << totalTargetAccessesBytes[j] << std::endl;//<< totalTargetAccessesBytes[i][j] << " / " << (totalTargetAccessesBytes[i][j] * BYTE_SIZE) << std::endl;
				totalAccesses += totalTargetAccessesBytes[j];

				/*for (const auto& [hash, accessedCount] : totalTargetAccessesBytes[j]) {
					PintoolOutput::accessLog << '\t' << StringifyHashedSequence(hash) << ": " << accessedCount << std::endl;
					totalAccesses += accessedCount;
				}*/
			}
		//}
		PintoolOutput::accessLog << "Total Software Implementation Accessed Bytes: " << totalAccesses << std::endl;

		PintoolOutput::accessLog << "Full Acessed Bytes (Uninstrumented Included): " << g_fullAccessCount << std::endl;

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
		//WriteEnergyConsumptionSavingsToLogFile(PintoolOutput::energyConsumptionLog, totalTargetEnergy, false, false, "	");

		PintoolOutput::accessLog.close();
	}

	VOID Fini(const INT32 code, VOID* v) {
		std::cout << "\nFinal Levels: " << levels.size() << " - ";
		for (const auto& l : levels) {
			std::cout << " " << l << ";";
		} 
		std::cout << std::endl;

		std::cout << "ApproxSS Fini: " << PintoolControl::g_mainThreadControl.HowManyActiveBuffers() << " buffer(s) still active at Fini." << std::endl;

		if (PintoolControl::g_mainThreadControl.HasActiveBuffer()) {
			PintoolControl::g_mainThreadControl.PrintStillActiveBuffers();
		}

		#if PIN_LOCKED
			for (const auto& [_, tdata] : PintoolControl::threadControlMap) {
				tdata->~ThreadControl();
			}
		#else
			PintoolControl::g_mainThreadControl.~ThreadControl();
		#endif

		//std::cout << "Writing Access Logs..." << std::endl;
		PintoolOutput::WriteAccessLog();

		//std::cout << "Writing Energy Logs?" << std::endl;
		if (!g_consumptionProfiles.empty()) {
			//std::cout << "Yes, Writing Energy Logs..." << std::endl;
			PintoolOutput::WriteEnergyLog();
		}

		//std::cout << "Deleting Data Estructures..." << std::endl;
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
	//RTN_AddInstrumentFunction(TargetInstrumentation::Routine, nullptr);

	// Obtain  a key for TLS storage.
	#if PIN_LOCKED
		g_tlsKey = PIN_CreateThreadDataKey(nullptr);
		if (g_tlsKey == INVALID_TLS_KEY)    {
			std::cerr << "Pin Error: number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << std::endl;
			PIN_ExitProcess(EXIT_FAILURE);
		}

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
