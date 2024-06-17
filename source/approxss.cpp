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

bool g_isGlobalInjectionEnabled = true;
int g_level = 0;
uint64_t g_injectionCalls = 0;
uint64_t g_currentPeriod = 0;

PIN_LOCK g_pinLock;


#if NARROW_ACCESS_INSTRUMENTATION
	bool IsInstrumentationActive = false;
	#define ASSERT_ACCESS_INSTRUMENTATION_ACTIVE() if (!IsInstrumentationActive) return; 
	#define SET_ACCESS_INSTRUMENTATION_STATUS(stat) IsInstrumentationActive = stat;
#else
	#define ASSERT_ACCESS_INSTRUMENTATION_ACTIVE()
	#define SET_ACCESS_INSTRUMENTATION_STATUS(stat)
#endif

std::ofstream g_accessOutputLog;
std::ofstream g_energyConsumptionOutputLog;

///////////////////////////////////////////////////////

#if LONG_TERM_BUFFER
	typedef LongTermApproximateBuffer ChosenTermApproximateBuffer;
#else
	typedef ShortTermApproximateBuffer ChosenTermApproximateBuffer;
#endif

typedef std::tuple<uint8_t const *, uint8_t const *, int64_t, int64_t, size_t> GeneralBufferRecord; //<Range, BufferId, ConfigurationId, dataSizeInBytes>
typedef std::map<GeneralBufferRecord, const std::unique_ptr<ChosenTermApproximateBuffer>> GeneralBuffers; 

GeneralBuffers g_generalBuffers;

#if MULTIPLE_ACTIVE_BUFFERS
	struct RangeCompare {
		//overlapping ranges are considered equivalent
		bool operator()(const Range& lhv, const Range& rhv) const {  
			return lhv.m_finalAddress <= rhv.m_initialAddress;
		} 
	};

	typedef std::map<Range, ChosenTermApproximateBuffer*, RangeCompare> ActiveBuffers;
	ActiveBuffers g_activeBuffers;
#else
	ChosenTermApproximateBuffer* g_activeBuffer = nullptr;
#endif

InjectorConfigurationMap	g_injectorConfigurations;
ConsumptionProfileMap 		g_consumptionProfiles;

/* ====================================================================	*/
/* Inspect each memory read and write									*/
/* ==================================================================== */

namespace AccessHandler {
	VOID CheckAndForward(const THREADID threadId, void (ChosenTermApproximateBuffer::*function)(uint8_t* const, const UINT32), uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
		PIN_GetLock(&g_pinLock, threadId);

		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator approxBuffer = g_activeBuffers.find(Range{accessedAddress, accessedAddress});
			if (approxBuffer != g_activeBuffers.cend()) {
				(*(approxBuffer->second).*function)(accessedAddress, accessSizeInBytes);
			}
		#else
			if (g_activeBuffer != nullptr && g_activeBuffer->DoesIntersectWith(accessedAddress)) {
				(*g_activeBuffer.*function)(accessedAddress, accessSizeInBytes);
			}
		#endif

		PIN_ReleaseLock(&g_pinLock);
	}

	// memory read
	VOID HandleMemoryReadSIMD(const THREADID threadId, uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {		
		CheckAndForward(threadId, &ChosenTermApproximateBuffer::HandleMemoryReadSIMD, accessedAddress, accessSizeInBytes);
	}

	VOID HandleMemoryRead(const THREADID threadId, uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {		
		CheckAndForward(threadId, &ChosenTermApproximateBuffer::HandleMemoryReadSingleElementSafe, accessedAddress, accessSizeInBytes);
	}

	// memory write
	VOID HandleMemoryWriteSIMD(const THREADID threadId, uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
		CheckAndForward(threadId, &ChosenTermApproximateBuffer::HandleMemoryWriteSIMD, accessedAddress, accessSizeInBytes);
	}

	VOID HandleMemoryWrite(const THREADID threadId, uint8_t* const accessedAddress, const UINT32 accessSizeInBytes) {
		CheckAndForward(threadId, &ChosenTermApproximateBuffer::HandleMemoryWriteSingleElementSafe, accessedAddress, accessSizeInBytes);
	}

	VOID CheckAndForwardScattered(const THREADID threadId, void (ChosenTermApproximateBuffer::*function)(uint8_t* const, const bool), IMULTI_ELEMENT_OPERAND const * const memOpInfo, const size_t errorCat) {
		if (memOpInfo->NumOfElements() < 1) {
			return;
		}
		
		uint8_t * accessedAddress = (uint8_t*) memOpInfo->ElementAddress(0); 
		ChosenTermApproximateBuffer* bufferP;

		PIN_GetLock(&g_pinLock, threadId);
		
		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator approxBuffer = g_activeBuffers.find(Range{accessedAddress, accessedAddress});
			if (approxBuffer != g_activeBuffers.cend()) {
				bufferP = approxBuffer->second;
			}
		#else
			if (g_activeBuffer != nullptr && g_activeBuffer->DoesIntersectWith(accessedAddress)) {
				bufferP = g_activeBuffer;
			}
		#endif
			 else {
				PIN_ReleaseLock(&g_pinLock);
				return;
			}

		const bool shouldInject = bufferP->GetShouldInject(errorCat);

		for (UINT32 i = 0; i < memOpInfo->NumOfElements(); ++i) {
			accessedAddress = (uint8_t*) memOpInfo->ElementAddress(i);
			(*bufferP.*function)(accessedAddress, shouldInject);
		}

		PIN_ReleaseLock(&g_pinLock);
	}

	VOID HandleMemoryReadScattered(const THREADID threadId, IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		CheckAndForwardScattered(threadId, &ChosenTermApproximateBuffer::HandleMemoryReadSingleElementUnsafe, memOpInfo, ErrorCategory::Read);
	}

	VOID HandleMemoryWriteScattered(const THREADID threadId, IMULTI_ELEMENT_OPERAND const * const memOpInfo) {
		CheckAndForwardScattered(threadId, &ChosenTermApproximateBuffer::HandleMemoryWriteSingleElementUnsafe, memOpInfo, ErrorCategory::Write);
	}
}

/* ==================================================================== */
/* ApproxSS Control														*/
/* ==================================================================== */

namespace PintoolControl {
	//i had to add the next two because i needed a simple and direct way of enabling and disabling the error injection
	VOID enable_global_injection(const THREADID threadId) {
		PIN_GetLock(&g_pinLock, threadId);

		g_isGlobalInjectionEnabled = true;

		PIN_ReleaseLock(&g_pinLock);
	}

	VOID disable_global_injection(const THREADID threadId) {
		PIN_GetLock(&g_pinLock, threadId);

		g_isGlobalInjectionEnabled = false;

		PIN_ReleaseLock(&g_pinLock);
	}

	VOID disable_access_instrumentation(const THREADID threadId) {
		PIN_GetLock(&g_pinLock, threadId);

		SET_ACCESS_INSTRUMENTATION_STATUS(false)

		PIN_ReleaseLock(&g_pinLock);
	}

	//effectively enables the error injection 
	VOID start_level(const THREADID threadId) {
		PIN_GetLock(&g_pinLock, threadId);

		++g_level;

		PIN_ReleaseLock(&g_pinLock);
	}

	//not a boolean to allow layers (so functions that call each other don't disable the injection)

	//effectively disables the error injection
	VOID end_level(const THREADID threadId) {
		PIN_GetLock(&g_pinLock, threadId);

		--g_level;

		PIN_ReleaseLock(&g_pinLock);
	}

	VOID next_period(const THREADID threadId) {
		PIN_GetLock(&g_pinLock, threadId);

		++g_currentPeriod;
		#if MULTIPLE_ACTIVE_BUFFERS
			for (const auto& [_, activeBuffer] : g_activeBuffers) {
				activeBuffer->NextPeriod(g_currentPeriod);
			}
		#else
			if (g_activeBuffer != nullptr) {
				g_activeBuffer->NextPeriod(g_currentPeriod);
			}
		#endif

		PIN_ReleaseLock(&g_pinLock);
	}

	VOID add_approx(const THREADID threadId, uint8_t * const start_address, uint8_t const * const end_address, const int64_t bufferId, const int64_t configurationId, const uint32_t dataSizeInBytes) {
		PIN_GetLock(&g_pinLock, threadId);

		const Range range = Range(start_address, end_address);

		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator lbActive = g_activeBuffers.lower_bound(range);
			if (!((lbActive != g_activeBuffers.cend()) && !(g_activeBuffers.key_comp()(range, lbActive->first)))) //only inserts if it wasn't found (done like this to avoid possible memory leaks from the new's in case there's a overlap)
		#else
			if (g_activeBuffer == nullptr)
		#endif
		{
			const GeneralBufferRecord generalBufferKey = std::make_tuple(range.m_initialAddress, range.m_finalAddress, bufferId, configurationId, dataSizeInBytes);
			const GeneralBuffers::const_iterator lbGeneral = g_generalBuffers.lower_bound(generalBufferKey);

			if ((lbGeneral != g_generalBuffers.cend()) && !(g_generalBuffers.key_comp()(generalBufferKey, lbGeneral->first))) {
				#if MULTIPLE_ACTIVE_BUFFERS
					ChosenTermApproximateBuffer* const approxBuffer = lbGeneral->second.get();
					approxBuffer->ReactivateBuffer(g_currentPeriod);
					g_activeBuffers.insert(lbActive, {range, approxBuffer});
				#else
					g_activeBuffer = lbGeneral->second.get();
					g_activeBuffer->ReactivateBuffer(g_currentPeriod);
				#endif
			} else {
				const InjectorConfigurationMap::const_iterator bcIt = g_injectorConfigurations.find(configurationId);

				if (bcIt == g_injectorConfigurations.cend()) {
					std::cout << ("ApproxSS Error: Configuration " + std::to_string(configurationId) + " not found.") << std::endl;
					std::exit(EXIT_FAILURE);
				}

				ChosenTermApproximateBuffer* const approxBuffer = new ChosenTermApproximateBuffer(range, bufferId, g_currentPeriod, dataSizeInBytes, *bcIt->second);

				#if MULTIPLE_ACTIVE_BUFFERS
					g_activeBuffers.insert(lbActive, {range, approxBuffer});
				#else
					g_activeBuffer = approxBuffer;
				#endif

				g_generalBuffers.emplace_hint(lbGeneral, generalBufferKey, std::unique_ptr<ChosenTermApproximateBuffer>(approxBuffer));
			}
		} else {
			std::cout << "ApproxSS Warning: approximate buffer (id: " << bufferId << ") already active. Ignoring addition request." << std::endl;
		}

		PIN_ReleaseLock(&g_pinLock);
	}

	VOID remove_approx(const THREADID threadId, uint8_t * const start_address, uint8_t const * const end_address, const bool giveAwayRecords) {
		PIN_GetLock(&g_pinLock, threadId);

		const Range range = Range(start_address, end_address);

		#if MULTIPLE_ACTIVE_BUFFERS
			const ActiveBuffers::const_iterator lbActive = g_activeBuffers.find(range); 
			if (lbActive != g_activeBuffers.cend() && lbActive->first.IsEqual(range)){
				lbActive->second->RetireBuffer(giveAwayRecords);
				g_activeBuffers.erase(lbActive);
			}
		#else
			if (g_activeBuffer != nullptr && g_activeBuffer->IsEqual(range)) {
				g_activeBuffer->RetireBuffer(giveAwayRecords);
				g_activeBuffer = nullptr;
			}
		#endif
		 else {
			std::cout << "ApproxSS Warning: approximate buffer not found for removal. Ignorning request." << std::endl;
		}

		PIN_ReleaseLock(&g_pinLock);
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
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadSIMD,
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryRead,
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYREAD_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryReadScattered,
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
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteSIMD,
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYWRITE_SIZE,
							IARG_END);
					} else {
						INS_InsertPredicatedCall(
							ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWrite,
							IARG_MEMORYOP_EA, memOp, IARG_MEMORYWRITE_SIZE,
							IARG_END);
					}
				} else {
					const UINT32 op = INS_MemoryOperandIndexToOperandIndex(ins, memOp);
					INS_InsertPredicatedCall(
						ins, IPOINT_BEFORE, (AFUNPTR)AccessHandler::HandleMemoryWriteScattered,
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
							IARG_THREAD_ID,
							IARG_FUNCARG_ENTRYPOINT_VALUE, 0,
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("end_level") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::end_level,  
							IARG_THREAD_ID, IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("next_period") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::next_period, 
							IARG_THREAD_ID,
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
							IARG_THREAD_ID,
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
							IARG_THREAD_ID,
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
							IARG_THREAD_ID,
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
							IARG_THREAD_ID,
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
							IARG_THREAD_ID,
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

		std::cout << std::string(50, '#') << std::endl;
	}

	void DeleteDataEstructures() {
		g_generalBuffers.clear();

		#if MULTIPLE_ACTIVE_BUFFERS
			g_activeBuffers.clear();
		#endif

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
			std::cout << ("ApproxSS Error: Unable to create output file: \"" + outputFilename + "\".") << std::endl;
			std::exit(EXIT_FAILURE);
		}
	}

	VOID WriteAccessLog() {
		g_accessOutputLog << "Final Level: " << g_level << std::endl;
		g_accessOutputLog << "Total Injection Calls: " << g_injectionCalls << std::endl;
		
		std::array<uint64_t, ErrorCategory::Size> totalTargetInjections;
		std::fill_n(totalTargetInjections.data(), ErrorCategory::Size, 0);

		std::array<uint64_t, AccessTypes::Size> totalTargetAccessesBytes;
		std::fill_n(totalTargetAccessesBytes.data(), AccessTypes::Size, 0);

		for (const auto& [_, approxBuffer] : g_generalBuffers) { 
			approxBuffer->WriteAccessLogToFile(g_accessOutputLog, totalTargetAccessesBytes, totalTargetInjections);
		}

		uint64_t totalAccesses = 0;
		g_accessOutputLog << std::endl;
		for (size_t i = 0; i < AccessTypes::Size; ++i) {
			g_accessOutputLog << "Total Software Implementation " << AccessTypesNames[i] << " Bytes/Bits: " << totalTargetAccessesBytes[i] << " / " << (totalTargetAccessesBytes[i] * BYTE_SIZE) << std::endl;
			totalAccesses += totalTargetAccessesBytes[i];
		}
		g_accessOutputLog << "Total Software Implementation Accessed Bytes/Bits: " << totalAccesses << " / " << (totalAccesses * BYTE_SIZE) << std::endl;

		#if LOG_FAULTS
			uint64_t totalInjections = 0;
			g_accessOutputLog << std::endl;

			for (size_t i = 0; i < ErrorCategory::Size; ++i) {
				std::string errorCat = ErrorCategoryNames[i];
				//StringHandling::toLower(errorCat);

				g_accessOutputLog << "Total " << errorCat << " Errors Injected: " << totalTargetInjections[i] << std::endl;
				totalInjections += totalTargetInjections[i];
			}

			g_accessOutputLog << "Total Errors Injected: " << (totalInjections) << std::endl;
		#endif
		
		g_accessOutputLog.close();
	}

	VOID WriteEnergyLog() {
		std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> totalTargetEnergy;
		std::fill_n(totalTargetEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, 0);

		g_energyConsumptionOutputLog.setf(std::ios::fixed);
		g_energyConsumptionOutputLog.precision(2);

		for (const auto& [_, approxBuffer] : g_generalBuffers) { 
			const int64_t configurationId = approxBuffer->GetConfigurationId();
			const ConsumptionProfileMap::const_iterator profileIt = g_consumptionProfiles.find(configurationId);

			if (profileIt == g_consumptionProfiles.cend()) {
				std::cout << "ApproxSS Error: somehow, Consumption Profile not informed." << std::endl;
				std::exit(EXIT_FAILURE);
			}

			const ConsumptionProfile& respectiveConsumptionProfile = *(profileIt->second.get());

			approxBuffer->WriteEnergyLogToFile(g_energyConsumptionOutputLog, totalTargetEnergy, respectiveConsumptionProfile);
		}

		g_energyConsumptionOutputLog << std::endl << "TARGET APPLICATION TOTAL ENERGY CONSUMPTION" << std::endl;
		WriteEnergyConsumptionToLogFile(g_energyConsumptionOutputLog, totalTargetEnergy, false, false, "	");
		WriteEnergyConsumptionSavingsToLogFile(g_energyConsumptionOutputLog, totalTargetEnergy, false, false, "	");

		g_accessOutputLog.close();
	}

	VOID Fini(const INT32 code, VOID* v) {
		#if MULTIPLE_ACTIVE_BUFFERS
			for (ActiveBuffers::const_iterator it = g_activeBuffers.cbegin(); it != g_activeBuffers.cend(); ) { 
				ChosenTermApproximateBuffer& approxBuffer = *(it->second);
				approxBuffer.RetireBuffer(false);
				it = g_activeBuffers.erase(it);
			}
		#else
			if (g_activeBuffer != nullptr) {
				g_activeBuffer->RetireBuffer(false);
				g_activeBuffer = nullptr;
			}
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
	PintoolOutput::CreateOutputLog(g_accessOutputLog, AccessOutputFile.Value(), "access.log");

	PintoolInput::ProcessEnergyProfile(EnergyProfileFile.Value());

	if (!g_consumptionProfiles.empty()) {
		PintoolOutput::CreateOutputLog(g_energyConsumptionOutputLog, EnergyConsumptionOutputFile.Value(), "energyConsumpion.log");
	}

	// Register Routine to be called to instrument rtn
	RTN_AddInstrumentFunction(TargetInstrumentation::Routine, 0);

	INS_AddInstrumentFunction(TargetInstrumentation::Instruction, 0);

	PIN_AddFiniFunction(PintoolOutput::Fini, 0);

	// Never returns
	PIN_StartProgram();
	
	return 0;
}
