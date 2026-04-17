#ifndef APPROX_GLOBALS_H
#define APPROX_GLOBALS_H

#include "pin.H"
#include <array>
#include <vector>
#include <map>
#include <stack>
#include <memory>
#include <string>

#include "compiling-options.h"
#include "configuration-input.h"
#include "approximate-buffer.h"
#include "precise-buffer.h"
#include "approximate-buffer-map-record.h"
#include "approximate-buffer-array-record.h"

// --- Types ---
typedef std::array<uint64_t, AccessTypes::Size> AccessCounter;
typedef std::map<int64_t, std::pair<AccessCounter, std::vector<int64_t>>> LayeredAccess; //<hash, <counter, layer list>>

#if LONG_TERM_BUFFER
    typedef ApproximateBufferArrayRecord ChosenTermApproximateBuffer;
#else
    typedef ApproximateBufferMapRecord ChosenTermApproximateBuffer;
#endif

typedef std::tuple<uint8_t const *, uint8_t const *, int64_t, int64_t, size_t, bool> GeneralBufferRecord; //<Range, BufferId, ConfigurationId, dataSizeInBytes, isPrecise>
typedef std::map<GeneralBufferRecord, const std::unique_ptr<BufferInterface>> GeneralBuffers; 

#if MULTIPLE_ACTIVE_BUFFERS
    /*struct RangeCompare {
        //overlapping ranges are considered equivalent
        bool operator()(const Range& lhv, const Range& rhv) const {  
            return lhv.m_finalAddress < rhv.m_initialAddress;
        } 
    };*/
    typedef std::map<Range, BufferInterface* /*, RangeCompare*/> ActiveBuffers;
#endif

// --- Macros ---
#if NARROW_ACCESS_INSTRUMENTATION
    extern bool IsInstrumentationActive;
    #define ASSERT_ACCESS_INSTRUMENTATION_ACTIVE() if (!IsInstrumentationActive) return; 
    #define SET_ACCESS_INSTRUMENTATION_STATUS(stat) IsInstrumentationActive = stat;
#else
    #define ASSERT_ACCESS_INSTRUMENTATION_ACTIVE()
    #define SET_ACCESS_INSTRUMENTATION_STATUS(stat)
#endif

// --- Extern Globals (Declarations Only) ---
extern std::vector<int64_t> g_levels;
extern int64_t g_sequenceHash;
extern std::stack<int64_t> g_layerHashes;
extern LayeredAccess g_layeredAccesses;
extern AccessCounter g_accessCounter;

extern uint64_t g_injectionCalls;
extern uint64_t g_currentPeriod;

#if BUFFERS_LAYERED_COUNTER
    extern LayeredAccess g_buffersLayeredAccesses;
    extern AccessCounter g_buffersAccessCounter;
#endif

#if PIN_LOCKED
    extern PIN_LOCK g_pinLock;
    extern TLS_KEY g_tlsKey;
#endif

extern InjectorConfigurationMap g_injectorConfigurations;
extern ConsumptionProfileMap g_consumptionProfiles;

// --- Helper Functions ---
inline int64_t HashValue(const int64_t value, const int64_t previous = 0) {
    return previous ^ (value + 0x9E3779B97F4A7C15 + (previous << 6) + (previous >> 2));
}

std::string StringifyLevels(const std::vector<int64_t>& layers);
void StoreAccessLayer(LayeredAccess& layeredAccess, AccessCounter& accessCounter, const std::vector<int64_t>& levels, const int64_t sequenceHash);

#endif /* APPROX_GLOBALS_H */