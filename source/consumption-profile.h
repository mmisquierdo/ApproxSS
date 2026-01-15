#ifndef CONSUMPTION_CONFIGURATION_H
#define CONSUMPTION_CONFIGURATION_H

#include <memory>
#include <map>

#include "compiling-options.h"

struct ConsumptionType { 
	static constexpr size_t Reference =		0;
	static constexpr size_t Approximate =	1; 
	static constexpr size_t Size =			2;
};

const std::array<const std::string, ConsumptionType::Size> ConsumptionTypeNames = {"REFERENCE", "APPROXIMATE"};

class ConsumptionProfile {
	const int64_t m_configurationId;
	bool m_hasReferenceValues;

	#if MULTIPLE_BER_CONFIGURATION
		std::array<std::array<std::unique_ptr<double[]>, 	ErrorCategory::Size>, ConsumptionType::Size> m_consumptionValues; //C++ é verboso, mas eu posso estar exagerando...
		std::array<size_t, ErrorCategory::Size> m_consumptionValuesCount;
	#else
		std::array<std::array<double,						ErrorCategory::Size>, ConsumptionType::Size> m_consumptionValues;
	#endif

	public:
		ConsumptionProfile(const int64_t configurationId);
		int64_t GetConfigurationId() const;
		bool HasReferenceValues() const;
		void SetHasReference(const bool hasReference);
		std::string toString(const std::string& lineStart = "") const;
		void StringfyConsumptionValues(std::string& s, const std::string& lineStart, const size_t consumptionType) const;

		double EstimateEnergyConsumption(const size_t processedBytes, const size_t bitDepth, const size_t dataSizeInBytes, const size_t consumptionTypeIndex, const size_t errorCat, const size_t berIndex = 0) const;

		#if MULTIPLE_BER_CONFIGURATION
			void SetConsumptionValue(const size_t consumptionType, const size_t errorCat, const size_t index, const double consumptionValue);
			void SetConsumptionValueCount(const size_t consumptionType, const size_t errorCat, const size_t count);
		#else
			void SetConsumptionValue(const size_t consumptionType, const size_t errorCat, const double consumptionValue);
			double GetConsumptionValue(const size_t consumptionType, const size_t errorCat) const;
		#endif
};

typedef std::map<int64_t, const std::unique_ptr<const ConsumptionProfile>> ConsumptionProfileMap; 
extern ConsumptionProfileMap g_consumptionProfiles;

#endif /* CONSUMPTION_CONFIGURATION_H */