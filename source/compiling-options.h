#ifndef COMPILING_OPTIONS_H
#define COMPILING_OPTIONS_H

constexpr size_t BYTE_SIZE = 8;

//USER-DEFINED START: just change true/false values, certain options are not compatible with others

#ifndef DEFAULT_FAULT_INJECTOR
	#define DEFAULT_FAULT_INJECTOR true
#endif

#ifndef GRANULAR_FAULT_INJECTOR
	#define GRANULAR_FAULT_INJECTOR (!DEFAULT_FAULT_INJECTOR && false)
#endif

#ifndef DISTANCE_BASED_FAULT_INJECTOR
	#define DISTANCE_BASED_FAULT_INJECTOR (!DEFAULT_FAULT_INJECTOR && !GRANULAR_FAULT_INJECTOR && false)
#endif

#ifndef LONG_TERM_BUFFER
	#define LONG_TERM_BUFFER true
#endif

#ifndef SHORT_TERM_BUFFER
	#define SHORT_TERM_BUFFER (!LONG_TERM_BUFFER && false)
#endif

#ifndef MULTIPLE_ACTIVE_BUFFERS
	#define MULTIPLE_ACTIVE_BUFFERS true
#endif

#ifndef NARROW_ACCESS_INSTRUMENTATION
	#define NARROW_ACCESS_INSTRUMENTATION false
#endif

#ifndef MULTIPLE_BER_CONFIGURATION
	#define MULTIPLE_BER_CONFIGURATION false
#endif

#ifndef MULTIPLE_BER_ELEMENT
	#define MULTIPLE_BER_ELEMENT (DEFAULT_FAULT_INJECTOR && false)
#endif

#ifndef ENABLE_PASSIVE_INJECTION
	#define ENABLE_PASSIVE_INJECTION false
#endif

#ifndef OVERCHARGE_BER
	#define OVERCHARGE_BER (ENABLE_PASSIVE_INJECTION && !MULTIPLE_BER_ELEMENT && !DISTANCE_BASED_FAULT_INJECTOR && !LOG_FAULTS && false)
#endif

#ifndef OVERCHARGE_FLIP_BACK
	#define OVERCHARGE_FLIP_BACK (OVERCHARGE_BER && false)
#endif

#ifndef LOG_FAULTS
	#define LOG_FAULTS true
#endif

#ifndef LS_BIT_DROPPING //premilinary version. TODO: take it as input from injector configuration
	#define LS_BIT_DROPPING (DEFAULT_FAULT_INJECTOR && false)
#endif

#ifndef PIN_SHARED_LOCKED
	#define PIN_SHARED_LOCKED false
#endif

#ifndef PIN_PRIVATE_LOCKED
	#define PIN_PRIVATE_LOCKED !PIN_SHARED_LOCKED && false
#endif

//USER-DEFINED END

#ifndef PIN_ANY_LOCKED
	#define PIN_ANY_LOCKED PIN_SHARED_LOCKED || PIN_PRIVATE_LOCKED
#endif

#if PIN_SHARED_LOCKED
	#define IF_PIN_SHARED_LOCKED(X) X
	#define IF_PIN_SHARED_LOCKED_COMMA(X) X,
#else
	#define IF_PIN_SHARED_LOCKED(X)
	#define IF_PIN_SHARED_LOCKED_COMMA(X)
#endif

#if PIN_PRIVATE_LOCKED
	#define IF_PIN_PRIVATE_LOCKED(X) X
	#define IF_PIN_PRIVATE_LOCKED_COMMA(X) X,
#else
	#define IF_PIN_PRIVATE_LOCKED(X)
	#define IF_PIN_PRIVATE_LOCKED_COMMA(X)
#endif

#if PIN_ANY_LOCKED
	#define IF_PIN_ANY_LOCKED(X) X
	#define IF_PIN_ANY_LOCKED_COMMA(X) X,
#else
	#define IF_PIN_ANY_LOCKED(X)
	#define IF_PIN_ANY_LOCKED_COMMA(X)
#endif


#if !DEFAULT_FAULT_INJECTOR && !GRANULAR_FAULT_INJECTOR && !DISTANCE_BASED_FAULT_INJECTOR
#	error "ApproxSS compilation error: no fault injector defined!"
#endif

#if !LONG_TERM_BUFFER && !SHORT_TERM_BUFFER
#	error "ApproxSS compilation error: no buffer term defined!"
#endif

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