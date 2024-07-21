#ifndef BUFFER_LOG_PERIOD_H
#define BUFFER_LOG_PERIOD_H

#include <cstdint>
#include <algorithm>
#include <stdlib.h> 
#include <fstream>
#include <array>

#include "compiling-options.h"
#include "injector-configuration.h"
#include "configuration-input.h"

class PeriodLog {
	public:
		uint64_t m_period;

		std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> m_accessedBytesCount;

		#if LOG_FAULTS
			std::array<std::unique_ptr<uint64_t[]>, ErrorCategory::Size> m_errorsCountsByBit;

			void WriteAndSumIndividualInjectionArray(std::ofstream& outputLog, const std::string errorType, const size_t bitDepth, uint64_t& bufferTotalInjected, uint64_t const * const injectedByBit, const std::string& basePadding = "") const;
		#endif

		#if MULTIPLE_BER_CONFIGURATION
			std::array<size_t, ErrorCategory::Size> m_berIndex;
		#endif

		void IncreaseAccess(const bool precision, const size_t type, const size_t size /*in bytes*/);

		void WriteBerIndexesToFile(std::ofstream& outputLog, const std::string& basePadding = "") const;

		PeriodLog(PeriodLog& other, const size_t bitDepth);
		PeriodLog(const uint64_t period, const InjectionConfigurationLocal& injectorCfg);

		uint64_t* GetErrorCountsByBit(const size_t errorCat) const;

		bool IsVirgin() const;

		void ResetCounts(const uint64_t period, const InjectionConfigurationLocal& injectorCfg);

		void WriteAccessLogToFile(std::ofstream& outputLog, const size_t bitDepth, const size_t dataSizeInBytes, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& bufferAccessedBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding = "") const;
		void WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& bufferEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t bufferSizeInBytes, const std::string& basePadding = "") const;
		void CalculateEnergyConsumptionByErrorCategory(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &periodEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t consumptionTypeIndex, const size_t errorCat, const size_t softwareProcessedBytes) const;
		void CalculatePeriodEnergyConsumption(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &periodEnergy, const ConsumptionProfile &respectiveConsumptionProfile, const size_t bitDepth, const size_t dataSizeInBytes, const size_t bufferSizeInBytes) const;
};

void WriteEnergyConsumptionToLogFile(std::ofstream &outputLog, const std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &energy, const bool hasReferenceValues, const bool checkNaN = true, const std::string &basePadding = "");
void WriteEnergyConsumptionSavingsToLogFile(std::ofstream &outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> &energy, const bool hasReferenceValues, const bool checkNaN = true, const std::string &basePadding = "");
void AddEnergyConsumption(std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& destination, const std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& source);
void WriteAccessedBytesToFile(std::ofstream& outputLog, const size_t bitDepth, const size_t dataSizeInBytes, const uint64_t accessedBytes, const std::string& accessedType, const std::string& accessScope, const std::string& padding = "");

#endif /* BUFFER_LOG_PERIOD_H */