#include "approx-globals.h"

// --- Global Variable Definitions (Memory Allocation) ---
std::vector<int64_t> g_levels;
int64_t g_sequenceHash = 0;
std::stack<int64_t> g_layerHashes;
LayeredAccess g_layeredAccesses;
AccessCounter g_accessCounter{0};

uint64_t g_injectionCalls = 0;
int64_t g_currentPeriod = 0;

#if BUFFERS_LAYERED_COUNTER
    LayeredAccess g_buffersLayeredAccesses;
    AccessCounter g_buffersAccessCounter{0};
#endif

#if PIN_LOCKED
    PIN_LOCK g_pinLock;
    TLS_KEY g_tlsKey = INVALID_TLS_KEY;
#endif

#if NARROW_ACCESS_INSTRUMENTATION
    bool IsInstrumentationActive = false;
#endif

InjectorConfigurationMap g_injectorConfigurations;
ConsumptionProfileMap g_consumptionProfiles;

// --- Helper Implementations ---
std::string StringifyLevels(const std::vector<int64_t>& layers) {
    std::string str;
    if (layers.empty()) return str;

    str += std::to_string(layers[0]);
    for (size_t i = 1; i < layers.size(); ++i) {
        str += '.' + std::to_string(layers[i]);
    }
    return str;
}

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