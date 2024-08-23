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

#if DISTANCE_BASED_FAULT_INJECTOR
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

class InjectionConfigurationReference;

class InjectionConfigurationBase {
	protected:
		int64_t m_configurationId;
		size_t m_bitDepth;

		#if LS_BIT_DROPPING
			size_t m_LSBDropped;
		#endif

		//CHANGE InjectionConfigurationLocal initializer if you add members	

	public:
		InjectionConfigurationBase(const InjectionConfigurationReference& other);
		InjectionConfigurationBase(const InjectionConfigurationBase& other);
		InjectionConfigurationBase();	


		int64_t GetConfigurationId() const;
		size_t GetBitDepth() const;

		#if LS_BIT_DROPPING
			size_t GetLSBDropped() const;
			void SetLSBDropped(const size_t lsbDropped);
		#endif

		void SetConfigurationId(const int64_t id);
		void SetBitDepth(const size_t bitDepth);

		static ErrorType GetZeroBerValue();
		static bool ShouldGoOn(const std::pair<double, double>& ber);
		static bool ShouldGoOn(const double ber);
		static bool ShouldGoOn(double const * const ber);
};

class InjectionConfigurationReference : public virtual InjectionConfigurationBase {
	private:
		#if MULTIPLE_BER_CONFIGURATION
			std::array<MultiBer, ErrorCategory::Size> m_bers;
		#else 
			std::array<ErrorTypeStore, ErrorCategory::Size> m_bers;
		#endif

	public: 
		InjectionConfigurationReference();

		std::string BerToString(const std::unique_ptr<double[]>& ber) const;
		std::string BerToString(const std::pair<double, double>& ber) const;
		std::string BerToString(const double ber) const;
		std::string BerToString(const MultiBer& ber) const;
		std::string toString(const std::string& lineStart = "") const;

		#if MULTIPLE_BER_CONFIGURATION
			ErrorType GetBer(const size_t errorCat, const size_t index) const;
			void SetBer(const size_t errorCat, const size_t index, ErrorTypeStore& ber);

			size_t GetBerCount(const size_t errorCat) const;
			void SetBerCount(const size_t errorCat, const size_t count);
			
		#else
			ErrorType GetBer(const size_t errorCat) const;
			void SetBer(const size_t errorCat, ErrorTypeStore& ber);
		#endif
};

class InjectionConfigurationLocal : public virtual InjectionConfigurationBase {
	private:
		std::array<bool, ErrorCategory::Size> m_shouldGoOn;

		std::array<ErrorType, ErrorCategory::Size> m_bers;

		#if MULTIPLE_BER_CONFIGURATION
			const InjectionConfigurationReference& m_reference;
			uint64_t m_creationPeriod;
		#endif

	public:
		InjectionConfigurationLocal(const InjectionConfigurationReference& reference);

		bool GetShouldGoOn(const size_t errorCat) const;
		void ReviseShouldGoOn(const size_t errorCat);
	
		ErrorType GetBer(const size_t errorCat) const;

		#if MULTIPLE_BER_CONFIGURATION
			ErrorType GetBer(const size_t errorCat, const size_t index) const;	

			uint64_t GetBerCurrentIndex(const size_t errorCat) const;
			uint64_t GetCreationPeriod() const;
			uint64_t GetBerIndex() const;
			uint64_t GetBerIndexFromPeriod(const uint64_t period) const;
			size_t GetBerCount(const size_t errorCat) const;

			void AdvanceBerIndex();
			void ResetBerIndex(const uint64_t newCreationPeriod);
			void UpdateBers();
			void UpdateBer(const size_t errorCat);
		#endif

		#if OVERCHARGE_BER 
			double GetBer(size_t errorCat, uint64_t pastIndex, const uint64_t currentIndex) const;
		#endif
};

typedef std::map<int64_t, const std::unique_ptr<const InjectionConfigurationReference>> InjectorConfigurationMap; 
extern InjectorConfigurationMap g_injectorConfigurations;

#endif /* INJECTOR_CONFIGURATION_H */