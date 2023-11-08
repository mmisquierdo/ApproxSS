#ifndef INJECTOR_CONFIGURATION_H
#define INJECTOR_CONFIGURATION_H

#include <string>
#include <cstdint>
#include <memory>
#include <map>
#include <sstream>
#include <array>

#include "compiling-options.h"

extern uint64_t g_currentPeriod;

#if CHOSEN_FAULT_INJECTOR == DISTANCE_BASED_FAULT_INJECTOR
	typedef std::pair<double, double> ErrorType; //<mean, std-dev>
#else
	#if MULTIPLE_BER_ELEMENT
		typedef double* ErrorType;
	#else
		typedef double ErrorType;
	#endif
#endif

#if MULTIPLE_BER_CONFIGURATION
	class MultiBer {
		public:
			std::unique_ptr<ErrorType[]> values;
			size_t count;

		MultiBer();

		#if MULTIPLE_BER_ELEMENT
			~MultiBer();
		#endif
	};
#endif

class InjectorConfiguration {
	private:
		int64_t m_configurationId;
		size_t m_bitDepth;

		std::array<ErrorType, ErrorCategory::Size> m_bers;
		std::array<bool, ErrorCategory::Size> m_shouldGoOn;

		#if MULTIPLE_BER_CONFIGURATION
			std::shared_ptr<std::array<MultiBer, ErrorCategory::Size>> m_bersArray;
			uint64_t m_creationPeriod;
		#endif

	public:
		InjectorConfiguration();

		int64_t GetConfigurationId() const;
		size_t GetBitDepth() const;
		bool GetShouldGoOn(const size_t errorCat) const;
		ErrorType GetBer(const size_t errorCat) const;
		static ErrorType GetZeroBerValue();

		void SetConfigurationId(const int64_t id);
		void SetBitDepth(const size_t bitDepth);
		void SetBer(const size_t errorCat, const ErrorType ber);

		static bool ShouldGoOn(const std::pair<double, double>& ber);
		static bool ShouldGoOn(const double ber);

		void ReviseShouldGoOn(const size_t errorCat);

		std::string SingleBerToString(double const * const ber) const;
		std::string SingleBerToString(const std::pair<double, double>& ber) const;
		std::string SingleBerToString(const size_t errorCat) const;

		std::string toString(const std::string& lineStart = "") const;

		#if MULTIPLE_BER_CONFIGURATION
			InjectorConfiguration(const InjectorConfiguration& other);

			uint64_t GetBerCurrentIndex(const size_t errorCat) const;
			ErrorType GetBer(const size_t errorCat, const size_t index) const;
			size_t GetBerCount(const size_t errorCat) const;
			uint64_t GetCreationPeriod() const;
			uint64_t GetBerIndex() const;
			uint64_t GetBerIndexFromPeriod(const uint64_t period) const;

			void SetBer(const size_t errorCat, const size_t index, const ErrorType ber);
			void SetBerCount(const size_t errorCat, const size_t count);

			void AdvanceBerIndex();
			void ResetBerIndex(const uint64_t newCreationPeriod);
			void ReviseBers();

			void ReviseBer(const size_t errorCat);

			const std::string MultipleBersToString(const size_t errorCat);
			InjectorConfiguration& operator=(const InjectorConfiguration& other);
		#endif

		#if CHOSEN_FAULT_INJECTOR != DISTANCE_BASED_FAULT_INJECTOR 
			double GetBer(size_t errorCat, uint64_t pastIndex, const uint64_t currentIndex) const;
		#endif
};

typedef std::map<int64_t, const std::unique_ptr<const InjectorConfiguration>> InjectorConfigurationMap; 
extern InjectorConfigurationMap g_injectorConfigurations;

#endif /* INJECTOR_CONFIGURATION_H */