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
#include "approximate-buffer-map-record.h"
#include "approximate-buffer-array-record.h"
#include "configuration-input.h"
#include "compiling-options.h"

#include "approx-globals.h"
#include "thread-control.h"
#include "pintool-control.h"
#include "access-handler.h"


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
							IARG_FUNCARG_ENTRYPOINT_VALUE, 5,
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
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		if (rtnName.find("disable_global_injection") != std::string::npos) {
			RTN_Open(rtn);
			RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::disable_global_injection,  
							IF_PIN_LOCKED_COMMA(IARG_THREAD_ID)
							IARG_END);
			RTN_Close(rtn);
			SET_ACCESS_INSTRUMENTATION_STATUS(true)
			return;
		}

		#if NARROW_ACCESS_INSTRUMENTATION
			if (rtnName.find("disable_access_instrumentation") != std::string::npos) {
				RTN_Open(rtn);
				RTN_InsertCall(	rtn, IPOINT_BEFORE, (AFUNPTR)PintoolControl::disable_access_instrumentation, 
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

		std::cout << "APPROXSS CONFIGURATIONS:" << std::endl;
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

		PintoolControl::g_generalBuffers.clear();

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
		outputLog << '\n' << header << " {" << std::endl;
		outputLog << "\tSoftware Implementation Read/Written Bytes By Level {" << std::endl;
		AccessCounter totalCounter{0};
		totalCounter.fill(0);
		for (const auto& [hash, layerInfo] : layeredAccess) {
			const AccessCounter& accessLayer = layerInfo.first;
			const std::vector<int64_t>& layerLevels = layerInfo.second;

			const bool all_zeros = std::all_of(std::begin(accessLayer), std::end(accessLayer), [](const int i){ return i == 0;});

			if (all_zeros) {
				continue;
			}

			outputLog << "\t\t" << StringifyLevels(layerLevels) << ": " << accessLayer[AccessTypes::Read] << " / " << accessLayer[AccessTypes::Write] << std::endl;

			for (size_t i = 0; i < accessLayer.size(); ++i) {
				totalCounter[i] += accessLayer[i];
			}
		}
		outputLog << "\t}\n";

		outputLog << "\n\tTotal Software Implementation Read/Written Bytes: " << totalCounter[AccessTypes::Read] << " / " << totalCounter[AccessTypes::Write] << std::endl;
		outputLog << "}\n";
	}

	VOID WriteAccessLog() {
		PintoolOutput::accessLog << "Total Injection Calls: " << g_injectionCalls << std::endl;
		
		std::array<uint64_t, ErrorCategory::Size> totalTargetInjections;
		std::fill_n(totalTargetInjections.data(), ErrorCategory::Size, 0);

		std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> totalTargetAccessesBytes;
		std::fill_n(&(totalTargetAccessesBytes[0][0]), AccessPrecision::Size * AccessTypes::Size, 0);

		for (const auto& [_, approxBuffer] : PintoolControl::g_generalBuffers) { 
			approxBuffer->WriteAccessLogToFile(PintoolOutput::accessLog, totalTargetAccessesBytes, totalTargetInjections);
		}

		uint64_t totalAccesses = 0;
		PintoolOutput::accessLog << std::endl;
		PintoolOutput::accessLog << "Instrumented Buffers Total {" << std::endl;
		for (size_t i = 0; i < AccessPrecision::Size; ++i) {
			for (size_t j = 0; j < AccessTypes::Size; ++j) {
				PintoolOutput::accessLog << "\tTotal Software Implementation " << AccessPrecisionNames[i] << " " << AccessTypesNames[j] << " Bytes: " << totalTargetAccessesBytes[i][j] << std::endl;
				totalAccesses += totalTargetAccessesBytes[i][j];
			}
		}

		PintoolOutput::accessLog << "\tTotal Software Implementation Accessed Bytes: " << totalAccesses << std::endl;
		PintoolOutput::accessLog << "}" << std::endl;

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

		WriteDownLayeredAccesses(PintoolOutput::accessLog, g_layeredAccesses, "Overall Application Layered Access");

		#if BUFFERS_LAYERED_COUNTER
			WriteDownLayeredAccesses(PintoolOutput::accessLog, g_buffersLayeredAccesses, "Instrumented Buffers Layered Access");
		#endif

		PintoolOutput::accessLog.close();
	}

	VOID WriteEnergyLog() {
		std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> totalTargetEnergy;
		std::fill_n(totalTargetEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, 0);

		//PintoolOutput::energyConsumptionLog.setf(std::ios::fixed);
		//PintoolOutput::energyConsumptionLog.precision(2);

		for (const auto& [_, approxBuffer] : PintoolControl::g_generalBuffers) { 
			const int64_t configurationId = approxBuffer->GetConfigurationId();
			const ConsumptionProfileMap::const_iterator profileIt = g_consumptionProfiles.find(configurationId);

			if (profileIt == g_consumptionProfiles.cend()) {
				std::cerr << "ApproxSS Error: somehow, Consumption Profile not informed." << std::endl;
				PIN_ExitProcess(EXIT_FAILURE);
			}

			const ConsumptionProfile& respectiveConsumptionProfile = *(profileIt->second.get());

			approxBuffer->WriteEnergyLogToFile(PintoolOutput::energyConsumptionLog, totalTargetEnergy, respectiveConsumptionProfile);
		}

		PintoolOutput::energyConsumptionLog << std::endl << "Target Application Total Energy Consumption {" << std::endl;
		WriteEnergyConsumptionToLogFile(PintoolOutput::energyConsumptionLog, totalTargetEnergy, false, false, "	");
		//WriteEnergyConsumptionSavingsToLogFile(PintoolOutput::energyConsumptionLog, totalTargetEnergy, false, false, "	");
		PintoolOutput::energyConsumptionLog << "}" << std::endl;

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

	unsigned int starting_seed;

	if (!RNGSeed.Value().empty()) {
		starting_seed = static_cast<unsigned int>(std::stoul(RNGSeed.Value()));
	} else {
		starting_seed = static_cast<unsigned int>(std::random_device{}());
	}

	std::cout << std::string(50, '#') << std::endl;
	std::cout << "ApproxSS initial seed: " << starting_seed << std::endl;
	
	FaultInjector::generator[0] = std::default_random_engine{starting_seed};
	
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
