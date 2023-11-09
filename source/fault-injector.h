#ifndef FAULT_INJECTOR_H
#define FAULT_INJECTOR_H

class ApproximateBuffer;

#include <random>
#include <cstring>
#include <algorithm>
#include <iostream>

#include "compiling-options.h"
#include "injector-configuration.h"

#if LOG_FAULTS
	#define AND_LOG_PARAMETER , uint64_t* const injectedByBit
	#define AND_LOG_ARGUMENT(X) , X
#else
	#define AND_LOG_PARAMETER
	#define AND_LOG_ARGUMENT(X)
#endif

extern uint64_t g_injectionCalls;

class FaultInjector : public InjectionConfigurationBorrower {
	protected: 
		static std::uniform_real_distribution<double> occurrenceDistribution;
		static constexpr uint8_t bitMask = 0x01;		
		static std::default_random_engine generator;

	public:
		FaultInjector(const InjectionConfigurationBorrower& injectorCfg);

		#if !MULTIPLE_BER_ELEMENT
			void InjectFault(uint8_t* const data, const double ber, ApproximateBuffer* const toBackup AND_LOG_PARAMETER);
		#else
			void InjectFault(uint8_t* const data, double const * const ber, ApproximateBuffer* const toBackup AND_LOG_PARAMETER);
		#endif

		#if OVERCHARGE_FLIP_BACK
			void InjectFaultOvercharged(uint8_t* const data, double ber AND_LOG_PARAMETER);
		#endif
};

class GranularFaultInjector : public FaultInjector {
	protected:
		std::uniform_int_distribution<size_t> m_instanceDistribution;
	
	public:
		GranularFaultInjector(const InjectionConfigurationBorrower& injectorCfg);

		void InjectFault(uint8_t* const data, const double ber, ApproximateBuffer* const toBackup AND_LOG_PARAMETER);

		#if OVERCHARGE_FLIP_BACK
			void InjectFaultOvercharged(uint8_t* const data, double ber AND_LOG_PARAMETER);
		#endif
};

#if CHOSEN_FAULT_INJECTOR == DISTANCE_BASED_FAULT_INJECTOR
	class DistanceBasedInjectorRecord {
		public:
			std::normal_distribution<double> m_errorDistanceDistribution;
			int64_t m_nextErrorDistance; //in bytes
			size_t m_injectionBit;

			DistanceBasedInjectorRecord(); //TODO: fix this gambiarra
			DistanceBasedInjectorRecord(const std::pair<double, double>& meanAndDev, const size_t dataSizeInBytes, const size_t bitDepth);

			bool IsEnabled() const;

			int64_t GenerateNewNextErrorDistance();
			void UpdateErrorDistanceAndInjectionBit(const size_t dataSizeInBytes, const size_t bitDepth);
	};

	class DistanceBasedFaultInjector : public FaultInjector {
		protected:
			const size_t m_dataSizeInBytes;

			#if MULTIPLE_BER_CONFIGURATION
				std::array<DistanceBasedInjectorRecord*,					ErrorCategory::Size> m_record;

				std::array<std::unique_ptr<DistanceBasedInjectorRecord[]>, 	ErrorCategory::Size> m_recordArray;
				std::array<size_t, 											ErrorCategory::Size> m_recordCount;
			#else
				std::array<DistanceBasedInjectorRecord, 					ErrorCategory::Size> m_record;
			#endif

		public:
			#if MULTIPLE_BER_CONFIGURATION
				void ReviseRecord(const size_t errorCat);

				void AdvanceBerIndex(); 
				void ResetBerIndex(const uint64_t newCreationPeriod);
				void ReviseRecords(); 

				DistanceBasedInjectorRecord* GetInjectorRecord(const size_t errorCat, const size_t index);
				void InjectFault(uint8_t* data, const size_t errorCat, const size_t recordIndex, const ssize_t accessSizeInBytes, ApproximateBuffer* const toBackup AND_LOG_PARAMETER);
			#endif
			
			DistanceBasedFaultInjector(const InjectionConfigurationBorrower& injectorCfg, const size_t dataSizeInBytes);

			DistanceBasedInjectorRecord* GetInjectorRecord(const size_t errorCat);

			void InjectFault(uint8_t* data, DistanceBasedInjectorRecord& injectorRecord, ssize_t accessSizeInBytes, ApproximateBuffer* const toBackup AND_LOG_PARAMETER); 
			void InjectFault(uint8_t* data, const size_t errorCat, const ssize_t accessSizeInBytes, ApproximateBuffer* const toBackup AND_LOG_PARAMETER);
	};
#endif

#include "approximate-buffer.h" //tendo que fazer isso pra resolver um bug na compilacao

#endif /*FAULT_INJECTOR_H*/