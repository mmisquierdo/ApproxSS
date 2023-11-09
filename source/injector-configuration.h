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
	typedef std::pair<double, double> ErrorTypeStore; //<mean, std-dev>
#else
	#if MULTIPLE_BER_ELEMENT
		typedef double* ErrorType;
		typedef std::unique_ptr<double[]> ErrorTypeStore;
	#else
		typedef double ErrorType;
		typedef double ErrorTypeStore;
	#endif
#endif

//#if MULTIPLE_BER_CONFIGURATION
	class MultiBer {
		public:
			std::unique_ptr<ErrorTypeStore[]> values;
			size_t count;

		MultiBer();
	};
//#endif

class InjectionConfigurationBase {
	private:
		int64_t m_configurationId;
		size_t m_bitDepth;

	public:
		int64_t GetConfigurationId() const;
		size_t GetBitDepth() const;

		void SetConfigurationId(const int64_t id);
		void SetBitDepth(const size_t bitDepth);

		static ErrorType GetZeroBerValue();
		static bool ShouldGoOn(const std::pair<double, double>& ber);
		static bool ShouldGoOn(const double ber);
		static bool ShouldGoOn(double const * const ber);
};

class InjectionConfigurationOwner : public virtual InjectionConfigurationBase {
	private:
		#if MULTIPLE_BER_CONFIGURATION
			std::array<MultiBer, ErrorCategory::Size> m_bers;
		#else 
			std::array<ErrorTypeStore[], ErrorCategory::Size> m_bers;
		#endif

	public: 
		InjectionConfigurationOwner();

		std::string BerToString(const std::unique_ptr<double[]>& ber) const;
		std::string BerToString(const std::pair<double, double>& ber) const;
		std::string BerToString(const double ber) const;
		std::string BerToString(const MultiBer& ber) const;
		std::string toString(const std::string& lineStart = "") const;

		#if MULTIPLE_BER_CONFIGURATION
			void SetBer(const size_t errorCat, const size_t index, const ErrorType ber);
			void SetBerCount(const size_t errorCat, const size_t count);
		#endif
};

class InjectionConfigurationBorrower : public virtual InjectionConfigurationBase {
	private:
		std::array<bool, ErrorCategory::Size> m_shouldGoOn;

		std::array<ErrorType, ErrorCategory::Size> m_bers;

		#if MULTIPLE_BER_CONFIGURATION
			const InjectionConfigurationOwner& m_owner;
			uint64_t m_creationPeriod;
		#endif

	public:
		InjectionConfigurationBorrower(const InjectionConfigurationOwner& owner);

		bool GetShouldGoOn(const size_t errorCat) const;
		void ReviseShouldGoOn(const size_t errorCat);
	
		ErrorType GetBer(const size_t errorCat) const;
		void SetBer(const size_t errorCat, const ErrorType ber);		

		#if MULTIPLE_BER_CONFIGURATION
			uint64_t GetBerCurrentIndex(const size_t errorCat) const;
			ErrorType GetBer(const size_t errorCat, const size_t index) const;
			size_t GetBerCount(const size_t errorCat) const;
			uint64_t GetCreationPeriod() const;
			uint64_t GetBerIndex() const;
			uint64_t GetBerIndexFromPeriod(const uint64_t period) const;

			void AdvanceBerIndex();
			void ResetBerIndex(const uint64_t newCreationPeriod);
			void ReviseBers();
			void ReviseBer(const size_t errorCat);
		#endif

		#if CHOSEN_FAULT_INJECTOR != DISTANCE_BASED_FAULT_INJECTOR 
			double GetBer(size_t errorCat, uint64_t pastIndex, const uint64_t currentIndex) const;
		#endif
};

typedef std::map<int64_t, const std::unique_ptr<const InjectionConfigurationOwner>> InjectorConfigurationMap; 
extern InjectorConfigurationMap g_injectorConfigurations;

#endif /* INJECTOR_CONFIGURATION_H */