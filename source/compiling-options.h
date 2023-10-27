#ifndef COMPILING_OPTIONS_H
#define COMPILING_OPTIONS_H

constexpr size_t BYTE_SIZE = 8;

#define DEFAULT_FAULT_INJECTOR 1
#define GRANULAR_FAULT_INJECTOR 2
#define DISTANCE_BASED_FAULT_INJECTOR 3

#define SHORT_TERM_BUFFER 1
#define LONG_TERM_BUFFER 2

/* DOEST NOT WORK WITH VSCODE TEXTVIEWER PREPROCESSING

constexpr size_t DEFAULT_FAULT_INJECTOR = 1;
constexpr size_t GRANULAR_FAULT_INJECTOR = 2;
constexpr size_t DISTANCE_BASED_FAULT_INJECTOR = 3;

constexpr size_t SHORT_TERM_BUFFER = 1;
constexpr size_t LONG_TERM_BUFFER = 2;*/

//USER-DEFINED START
#ifndef CHOSEN_FAULT_INJECTOR
	#define CHOSEN_FAULT_INJECTOR DEFAULT_FAULT_INJECTOR
#endif

#ifndef CHOSEN_TERM_BUFFER
	#define CHOSEN_TERM_BUFFER LONG_TERM_BUFFER
#endif

#ifndef MULTIPLE_ACTIVE_BUFFERS
	#define MULTIPLE_ACTIVE_BUFFERS false
#endif

#ifndef NARROW_ACCESS_INSTRUMENTATION
	#define NARROW_ACCESS_INSTRUMENTATION false
#endif

#ifndef ENABLE_PASSIVE_INJECTION
	#define ENABLE_PASSIVE_INJECTION false
#endif

#ifndef OVERCHARGE_FLIP_BACK
	#define OVERCHARGE_FLIP_BACK (ENABLE_PASSIVE_INJECTION && false)
#endif

#ifndef MULTIPLE_BERS
	#define MULTIPLE_BERS false
#endif

#ifndef LOG_FAULTS
	#define LOG_FAULTS false
#endif
//USER-DEFINED END


//AUXILIARY DATA STRUCTURES
#include <string>
#include <array>
struct ErrorCategory {
	static constexpr size_t Read	= 0;
	static constexpr size_t Write	= 1;
	#if ENABLE_PASSIVE_INJECTION 
		static constexpr size_t Passive	= 2;
		static constexpr size_t Size	= 3;
	#else
		static constexpr size_t Size	= 2;
	#endif
};

const std::array<const std::string, 3> ErrorCategoryNames = {"Read", "Write", "Passive"};

struct AccessTypes {
	static constexpr size_t Read	= 0;
	static constexpr size_t Write	= 1;
	static constexpr size_t Size	= 2;
};

const std::array<const std::string, AccessTypes::Size> AccessTypesNames = {"Read", "Write"};

#endif /* COMPILING_OPTIONS_H */