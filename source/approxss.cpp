/*
 *  This file contains an ISA-portable PIN tool for injecting memory faults.
 */

#include <stdio.h>
#include "pin.H"
#include <set>
#include <map>
#include <stack>
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
std::vector<int64_t> g_levels; //main-initialized
int64_t g_sequenceHash = 0; //main-initialized
std::stack<int64_t> g_layerHashes;

typedef std::array<uint64_t, AccessTypes::Size> AccessCounter;

typedef std::map<int64_t, std::pair<AccessCounter, std::vector<int64_t>>> LayeredAccess; //<hash, <counter, layer list>>

int64_t HashValue(const int64_t value, const int64_t previous = 0) {
	return previous ^ (value + 0x9E3779B97F4A7C15 + (previous << 6) + (previous >> 2));
}

/*int64_t HashArray(const std::vector<int64_t>& sequence) {
    int64_t result = 0;

    for (const auto& value : sequence) {
        result = HashValue(value, result)
    }

    return result;
}*/

std::string StringifyLevels(const std::vector<int64_t>& layers) {
	std::string str;

	if (layers.empty()) return str;

	str += std::to_string(layers[0]);

	for (size_t i = 1; i < layers.size(); ++ i) {
		str += '.' + std::to_string(layers[i]);
	}

	return str;
}

LayeredAccess g_layeredAccesses;
AccessCounter g_accessCounter{0};

#if BUFFERS_LAYERED_COUNTER
	LayeredAccess g_buffersLayeredAccesses;
	AccessCounter g_buffersAccessCounter;
#endif

void StoreAccessLayer(LayeredAccess& layeredAccess, AccessCounter& accessCounter, const std::vector<int64_t>& levels, const int64_t sequenceHash) {
	const LayeredAccess::iterator lbLayeredAccess = layeredAccess.lower_bound(sequenceHash);
	if (!((lbLayeredAccess != layeredAccess.cend()) && !(layeredAccess.key_comp()(sequenceHash, lbLayeredAccess->first)))) {
		layeredAccess.emplace_hint(lbLayeredAccess, sequenceHash, std::make_pair(accessCounter, levels));
	} else {
		AccessCounter& toUpdate = lbLayeredAccess->second.first;
		for (size_t i = 0; i < accessCounter.size(); ++i) {
			toUpdate[i] += accessCounter[i];
		}
	}
	accessCounter.fill(0);
}

uint64_t g_injectionCalls 	= 0; //NOTE: possible race condition, but I don't care

uint64_t g_currentPeriod 	= 0; //NOTE: possible minor race condition, but 99.9999% inconsequential and also actually impossible in current lock implementation

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

	bool HasActiveBuffer() const {
		return 
		#if MULTIPLE_ACTIVE_BUFFERS
			(!this->m_activeBuffers.empty())
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

	uint64_t HowManyActiveBuffers() const {
		#if MULTIPLE_ACTIVE_BUFFERS
			return this->m_activeBuffers.size();
		#else
			return this->m_activeBuffer ? 1 ? 0;
		#endif
	}

	void PrintStillActiveBuffers() const {
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

	//effectively disables the error injection
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

	VOID add_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const int64_t bufferId, const int64_t configurationId, const uint32_t dataSizeInBytes) {
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
				std::cout << "ApproxSS Warning: approximate buffer (id: " << bufferId << ") already active. Ignoring addition request." << std::endl;
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
	}

	VOID remove_approx(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t * const start_address, uint8_t const * const end_address, const bool giveAwayRecords) {
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
			  else {
				std::cout << "ApproxSS Warning: approximate buffer [" << (size_t) start_address << "; " << (size_t) end_address << "] not found for removal. Ignorning request." << std::endl;
			}
		#endif

		IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
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
			if (!PintoolControl::g_mainThreadControl.HasActiveBuffer())	{
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
				//ChosenTermApproximateBuffer& approxBuffer = *(it->second);
				//const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
				//(approxBuffer.*function)(accessedAddress, accessSizeInBytes, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
			
				#if BUFFERS_LAYERED_COUNTER
					g_buffersAccessCounter[accessType] += accessSizeInBytes;
				#endif
			}
		#else
			if (mainThread.m_activeBuffer != nullptr && mainThread.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
				//ChosenTermApproximateBuffer& approxBuffer = *(mainThread.m_activeBuffer);
				//const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
				//(approxBuffer.*function)(accessedAddress, accessSizeInBytes, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
			
				#if BUFFERS_LAYERED_COUNTER
					g_buffersAccessCounter[accessType] += accessSizeInBytes;
				#endif
			}
		#endif

		IF_PIN_LOCKED(PIN_ReleaseLock(&g_pinLock);)
	}

	// memory read
	VOID HandleMemoryReadSIMD(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
		g_accessCounter[AccessTypes::Read] += accessSizeInBytes;
		CheckAndForward(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSIMD, accessedAddress, accessSizeInBytes IF_COMMA_BUFFER_LAYERED(AccessTypes::Read));
	}

	VOID HandleMemoryRead(IF_PIN_LOCKED_COMMA(const THREADID threadId) uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {	
		g_accessCounter[AccessTypes::Read] += accessSizeInBytes;	
		CheckAndForward(IF_PIN_LOCKED_COMMA(threadId) &ChosenTermApproximateBuffer::HandleMemoryReadSingleElementSafe, accessedAddress, accessSizeInBytes IF_COMMA_BUFFER_LAYERED(AccessTypes::Read));
	}

	// memory write
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
			if (!PintoolControl::g_mainThreadControl.HasActiveBuffer())	{
				return;
			}
		#endif

		if (memOpInfo->NumOfElements() < 1) {
			return;
		}
		
		uint8_t * accessedAddress = (uint8_t*) memOpInfo->ElementAddress(0); 
		ThreadControl& mainThread = PintoolControl::g_mainThreadControl;

		#if MULTIPLE_ACTIVE_BUFFERS || PIN_LOCKED
			const Range range = Range(accessedAddress, accessedAddress); // MAKING ASSUMPTION THAT ALL THE ACCESS ARE CONTAINED IN THE BUFFER
		#endif
		
		IF_PIN_LOCKED(PIN_GetLock(&g_pinLock, -1);)
		
		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator it = mainThread.m_activeBuffers.find(range);
			if (it != mainThread.m_activeBuffers.cend()) {
				//ChosenTermApproximateBuffer& approxBuffer = *(it->second);
				//const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
				//(approxBuffer.*function)(memOpInfo, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
			
				#if BUFFERS_LAYERED_COUNTER
					g_buffersAccessCounter[accessType] += memOpInfo->NumOfElements() * memOpInfo->ElementSize(0);
				#endif
			}
		#else
			if (mainThread.m_activeBuffer != nullptr && mainThread.m_activeBuffer->DoesIntersectWith(accessedAddress)) {
				//ChosenTermApproximateBuffer& approxBuffer = *(mainThread.m_activeBuffer);
				//const ThreadControl& interestControl = AccessHandler::GetInterestThreadControl(IF_PIN_LOCKED(threadId));
				//(approxBuffer.*function)(memOpInfo, interestControl.isThreadInjectionEnabled() IF_COMMA_PIN_LOCKED(AccessHandler::IsPresent(interestControl, range)));
			
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
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadSIMD, IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryRead, IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadScattered, IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteSIMD, IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYWRITE_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWrite, IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYWRITE_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteScattered, IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
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
		PintoolOutput::PrintEnabledOrDisabled("Least significant bits dropping", LSB_DROPPING);
		PintoolOutput::PrintEnabledOrDisabled("Multithreading support: shared buffer list, thread-level control", PIN_LOCKED);

		std::cout << std::string(50, '#') << std::endl;
	}

	void DeleteDataEstructures() { 
		#if PIN_LOCKED
			PintoolControl::threadControlMap.clear();
		#endif

		PintoolControl::generalBuffers.clear();

		g_injectorConfigurations.clear();
	}

	std::string GenerateTimeDependentFileName(const std::string& suffix) {
		const std::time_t currentTime = std::time(nullptr);
		std::stringstream outputFilenameStream;
		outputFilenameStream << "ApproxSSOutputLog_" << std::put_time(std::localtime(&currentTime), "%Y:%m:%d:%H:%M:%S") << ':' << getpid() << '_' << suffix;
		return outputFilenameStream.str();
	}

	void CreateOutputLog(std::ofstream& outputFile, std::string outputFilename, const std::string& suffix) {
		if (outputFilename.empty()) {
			outputFilename = PintoolOutput::GenerateTimeDependentFileName(suffix);
		} else {
			if (outputFilename.back() == '/') {
				outputFilename += PintoolOutput::GenerateTimeDependentFileName(suffix);
			}
		}
		
		outputFile.open(outputFilename, std::ofstream::trunc);

		if (!outputFile) {
			std::cerr << "ApproxSS Error: Unable to create output file: \"" << outputFilename + "\"." << std::endl;
			PIN_ExitProcess(EXIT_FAILURE);
		}
	}

	VOID WriteDownLayeredAccesses(std::ofstream& outputLog, const LayeredAccess& layeredAccess, const std::string& header) {
		outputLog << '\n' << header << std::endl;
		outputLog << "Software Implementation Read/Written Bytes By Level: " << std::endl;
		AccessCounter totalCounter{0};
		totalCounter.fill(0);
		for (const auto& [hash, layerInfo] : layeredAccess) {
			const AccessCounter& accessLayer = layerInfo.first;
			const std::vector<int64_t>& layerLevels = layerInfo.second;

			const bool all_zeros = std::all_of(std::begin(accessLayer), std::end(accessLayer), [](const int i){ return i == 0;});

			if (all_zeros) {
				continue;
			}

			outputLog << '\t' << StringifyLevels(layerLevels) << ": " << accessLayer[AccessTypes::Read] << " / " << accessLayer[AccessTypes::Write] << std::endl;

			for (size_t i = 0; i < accessLayer.size(); ++i) {
				totalCounter[i] += accessLayer[i];
			}
		}
		outputLog << "Total Software Implementation Read/Written Bytes: " << totalCounter[AccessTypes::Read] << " / " << totalCounter[AccessTypes::Write] << std::endl;
	}

	VOID WriteAccessLog() {
		/*PintoolOutput::accessLog << "Total Injection Calls: " << g_injectionCalls << std::endl;
		
		std::array<uint64_t, ErrorCategory::Size> totalTargetInjections;
		std::fill_n(totalTargetInjections.data(), ErrorCategory::Size, 0);

		std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> totalTargetAccessesBytes;
		std::fill_n(&(totalTargetAccessesBytes[0][0]), AccessPrecision::Size * AccessTypes::Size, 0);

		for (const auto& [_, approxBuffer] : PintoolControl::generalBuffers) { 
			approxBuffer->WriteAccessLogToFile(PintoolOutput::accessLog, totalTargetAccessesBytes, totalTargetInjections);
		}

		uint64_t totalAccesses = 0;
		PintoolOutput::accessLog << std::endl;
		PintoolOutput::accessLog << "INSTRUMENTED BUFFERS" << std::endl;
		for (size_t i = 0; i < AccessPrecision::Size; ++i) {
			for (size_t j = 0; j < AccessTypes::Size; ++j) {
				PintoolOutput::accessLog << "Total Software Implementation " << AccessPrecisionNames[i] << " " << AccessTypesNames[j] << " Bytes/Bits: " << totalTargetAccessesBytes[i][j] << " / " << (totalTargetAccessesBytes[i][j] * BYTE_SIZE) << std::endl;
				totalAccesses += totalTargetAccessesBytes[i][j];
			}
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
		#endif*/

		WriteDownLayeredAccesses(PintoolOutput::accessLog, g_layeredAccesses, "OVERALL APPLICATION LAYERED ACCESS");

		#if BUFFERS_LAYERED_COUNTER
			WriteDownLayeredAccesses(PintoolOutput::accessLog, g_buffersLayeredAccesses, "INSTRUMENTED BUFFERS LAYERED ACCESS");
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
		StoreAccessLayer(g_layeredAccesses, g_accessCounter, g_levels, g_sequenceHash);
		#if BUFFERS_LAYERED_COUNTER
			StoreAccessLayer(g_buffersLayeredAccesses, g_buffersAccessCounter, g_levels, g_sequenceHash);
		#endif
		g_levels.pop_back();

		std::cout << "\nFinal Level: " << PintoolControl::g_mainThreadControl.m_level << std::endl; //" - "; //TODO: do this per thread later!!!
		/*for (const auto& l : levels) {
			std::cout << " " << l << ";";
		} */
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

		PintoolOutput::WriteAccessLog();

		/*if (!g_consumptionProfiles.empty()) {
			PintoolOutput::WriteEnergyLog();
		}*/

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

KNOB<std::string> InjectorConfigurationFile(	KNOB_MODE_WRITEONCE, "pintool", "cfg", 	"", "specify the error injector configuration file");
KNOB<std::string> EnergyProfileFile(			KNOB_MODE_WRITEONCE, "pintool", "pfl", 	"", "specify the energy consumption profile");
KNOB<std::string> AccessOutputFile(				KNOB_MODE_WRITEONCE, "pintool", "aof", 	"", "specify the memory access output log");
KNOB<std::string> EnergyConsumptionOutputFile(	KNOB_MODE_WRITEONCE, "pintool", "cof", 	"", "specify the energy consumpion output log");
KNOB<std::string> RNGSeed(						KNOB_MODE_WRITEONCE, "pintool", "seed", "", "specify the initial (pseudo)random number generation seed (unsigned int)");

/* ==================================================================== */
/* Main																	*/
/* ==================================================================== */

int main(const int argc, char* argv[]) {
	// Initialize symbol table code, needed for rtn instrumentation
	PIN_InitSymbols();
	if (PIN_Init(argc, argv)) return Usage();

	// Initialize ApproxSS access tracking structures
	g_levels.push_back(-1);
	g_sequenceHash = HashValue(g_levels.back());

	// Etc
	srand((unsigned)getpid() * (unsigned)time(0));

	if (!RNGSeed.Value().empty()) {
		FaultInjector::generator[0] = std::default_random_engine{static_cast<unsigned int>(std::stoul(RNGSeed.Value()))};
	} else {
		FaultInjector::generator[0] = std::default_random_engine{static_cast<unsigned int>(std::random_device{}())};
	}

	std::uniform_int_distribution<unsigned int> dist(std::numeric_limits<unsigned int>::min(), std::numeric_limits<unsigned int>::max());

	for (size_t i = 1; i < FaultInjector::genSize; ++i) {
		FaultInjector::generator[i] = std::default_random_engine{dist(FaultInjector::generator[0])};
	}

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
