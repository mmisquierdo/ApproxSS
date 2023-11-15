#include "injector-configuration.h"


#if MULTIPLE_BER_CONFIGURATION
	MultiBer::MultiBer() {
		this->values = std::unique_ptr<ErrorTypeStore[]>(new ErrorTypeStore[1]()); //zero-initialized
		//this->values[0](InjectionConfigurationBase::GetZeroBerValue());
		this->count = 1;
	}
#endif

ErrorType InjectionConfigurationBase::GetZeroBerValue() {
	#if DISTANCE_BASED_FAULT_INJECTOR
		return {0, 0};
	#else
		#if MULTIPLE_BER_ELEMENT
			return nullptr;
		#else
			return 0;
		#endif
	#endif
}

InjectionConfigurationBase::InjectionConfigurationBase() {
	this->m_configurationId = 0;
	this->m_bitDepth = 8;
}

int64_t InjectionConfigurationBase::GetConfigurationId() const {
	return this->m_configurationId;
}

size_t InjectionConfigurationBase::GetBitDepth() const {
	return this->m_bitDepth;
}

void InjectionConfigurationBase::SetBitDepth(const size_t bitDepth) {
	this->m_bitDepth = bitDepth;
}

void InjectionConfigurationBase::SetConfigurationId(const int64_t id) {
	this->m_configurationId = id;
}

bool InjectionConfigurationBase::ShouldGoOn(const std::pair<double, double>& ber) {
	return !(ber.first == 0 && ber.second == 0);
}

bool InjectionConfigurationBase::ShouldGoOn(const double ber) {
	return ber != 0;
}

bool InjectionConfigurationBase::ShouldGoOn(double const * const ber) {
	return ber != nullptr;
}

/////////////////////////////////////////////////////////////////////////
//					InjectionConfigurationReference
/////////////////////////////////////////////////////////////////////////

InjectionConfigurationReference::InjectionConfigurationReference() : InjectionConfigurationBase() { //eu realmente preciso disso?
	for (size_t i = 0; i < ErrorCategory::Size; ++i) {
		#if MULTIPLE_BER_CONFIGURATION
			this->m_bers[i] = MultiBer();
		#else
			#if MULTIPLE_BER_ELEMENT
				this->m_bers[i] = ErrorTypeStore(InjectionConfigurationBase::GetZeroBerValue());
			#else	
				this->m_bers[i] = InjectionConfigurationBase::GetZeroBerValue();
			#endif
		#endif
	}
}

std::string InjectionConfigurationReference::BerToString(const std::unique_ptr<double[]>& ber) const {
	std::string s;
	if (ber.get()) {
		for (size_t i = 0; i < (this->GetBitDepth() - 1); ++i) {
			s += std::to_string(ber[i]) + ", ";
		}
		s += std::to_string(ber[this->GetBitDepth()-1]);
	} else {
		return s += std::to_string(0.0);
	}
	return (s + ';'); 
}

std::string InjectionConfigurationReference::BerToString(const std::pair<double, double>& ber) const {
	return std::to_string(ber.first) + " " + std::to_string(ber.second) + ";"; 
}

std::string InjectionConfigurationReference::BerToString(const double ber) const {
	return std::to_string(ber) + ";"; 
}

std::string InjectionConfigurationReference::BerToString(const MultiBer& ber) const {
	std::stringstream s;
	for (size_t i = 0; i < ber.count; ++i) {
		s << " " << InjectionConfigurationReference::BerToString(ber.values[i]);
	}

	return s.str();
}

std::string InjectionConfigurationReference::toString(const std::string& lineStart /*= ""*/) const {
	std::string s = "";
	s += lineStart + "ConfigurationId: "	+ std::to_string(this->GetConfigurationId())	+ "\n";
	s += lineStart + "BitDepth: "			+ std::to_string(this->GetBitDepth())			+ "\n";

	for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
		s += lineStart + ErrorCategoryNames[errorCat] + "Ber: "
			+ InjectionConfigurationReference::BerToString(this->m_bers[errorCat])
			+ "\n";
	}

	return s;
}

#if MULTIPLE_BER_CONFIGURATION
	ErrorType InjectionConfigurationReference::GetBer(const size_t errorCat, const size_t index) const {
		const auto& ber = this->m_bers[errorCat].values[index % this->GetBerCount(errorCat)];
		#if MULTIPLE_BER_ELEMENT
			return ber.get();
		#else
			return ber;
		#endif
	}

	void InjectionConfigurationReference::SetBer(const size_t errorCat, const size_t index,	ErrorTypeStore& ber) {
		#if MULTIPLE_BER_ELEMENT
			this->m_bers[errorCat].values[index] = ErrorTypeStore(ber.release());
		#else
			this->m_bers[errorCat].values[index] = ber;
		#endif
	}

	size_t InjectionConfigurationReference::GetBerCount(const size_t errorCat) const {
		return this->m_bers[errorCat].count;
	}

	void InjectionConfigurationReference::SetBerCount(const size_t errorCat, const size_t count) {
		this->m_bers[errorCat].count = count;
		this->m_bers[errorCat].values = std::unique_ptr<ErrorTypeStore[]>(new ErrorTypeStore[count]); //TODO: should zero-initialize?
	}

#else

	ErrorType InjectionConfigurationReference::GetBer(const size_t errorCat) const {
		const auto& ber = this->m_bers[errorCat];
		#if MULTIPLE_BER_ELEMENT
			return ber.get();
		#else
			return ber;
		#endif
	}

	void InjectionConfigurationReference::SetBer(const size_t errorCat, ErrorTypeStore& ber) {
		#if MULTIPLE_BER_ELEMENT
			this->m_bers[errorCat] = ErrorTypeStore(ber.release());
		#else
			this->m_bers[errorCat] = ber;
		#endif
	}
#endif

/////////////////////////////////////////////////////////////////////////
//					InjectionConfigurationLocal
/////////////////////////////////////////////////////////////////////////

#if MULTIPLE_BER_CONFIGURATION
	InjectionConfigurationLocal::InjectionConfigurationLocal(const InjectionConfigurationReference& reference) : InjectionConfigurationBase(reference), m_reference(reference) {
		this->m_creationPeriod = g_currentPeriod;
		this->UpdateBers();
	}
#else
	InjectionConfigurationLocal::InjectionConfigurationLocal(const InjectionConfigurationReference& reference) : InjectionConfigurationBase(reference) {
		for (size_t i = 0; i < ErrorCategory::Size; ++i) {
			this->m_bers[i] = reference.GetBer(i);
			this->ReviseShouldGoOn(i);
		}
	}
#endif

void InjectionConfigurationLocal::ReviseShouldGoOn(const size_t errorCat) {
	#if MULTIPLE_BER_CONFIGURATION && ENABLE_PASSIVE_INJECTION
		//this->m_shouldGoOn[errorCat] = (this->ShouldGoOn(this->GetBer(errorCat)) && errorCat != ErrorCategory::Passive) || (this->ShouldGoOn(this->GetBer(errorCat)) && errorCat == ErrorCategory::Passive && this->GetBerCount(errorCat) <= 1);
		this->m_shouldGoOn[errorCat] = this->ShouldGoOn(this->GetBer(errorCat)) && (errorCat != ErrorCategory::Passive || this->GetBerCount(errorCat) <= 1);
	#else
		this->m_shouldGoOn[errorCat] = this->ShouldGoOn(this->GetBer(errorCat));
	#endif
}

bool InjectionConfigurationLocal::GetShouldGoOn(const size_t errorCat) const {
	return this->m_shouldGoOn[errorCat];
}

ErrorType InjectionConfigurationLocal::GetBer(const size_t errorCat) const {
	return this->m_bers[errorCat];
}

#if MULTIPLE_BER_CONFIGURATION
	ErrorType InjectionConfigurationLocal::GetBer(const size_t errorCat, const size_t index) const {
		return this->m_reference.GetBer(errorCat, index);
	}

	void InjectionConfigurationLocal::AdvanceBerIndex() {
		this->UpdateBers();
	}

	void InjectionConfigurationLocal::ResetBerIndex(const uint64_t newCreationPeriod) {
		this->m_creationPeriod = newCreationPeriod;
		this->UpdateBers();
	}

	void InjectionConfigurationLocal::UpdateBers() {
		for (size_t i = 0; i < ErrorCategory::Size; i++) {
			this->UpdateBer(i);
		}
	}

	void InjectionConfigurationLocal::UpdateBer(const size_t errorCat) {
		this->m_bers[errorCat] = this->m_reference.GetBer(errorCat, this->GetBerIndex());
		this->ReviseShouldGoOn(errorCat);
	}

	size_t InjectionConfigurationLocal::GetBerCount(const size_t errorCat) const {
		return this->m_reference.GetBerCount(errorCat);
	}

	uint64_t InjectionConfigurationLocal::GetBerCurrentIndex(const size_t errorCat) const {
		return (this->GetBerIndex() % this->GetBerCount(errorCat));
	}

	uint64_t InjectionConfigurationLocal::GetCreationPeriod() const {
		return this->m_creationPeriod;
	}

	uint64_t InjectionConfigurationLocal::GetBerIndex() const {
		return g_currentPeriod - this->GetCreationPeriod();
	}

	uint64_t InjectionConfigurationLocal::GetBerIndexFromPeriod(const uint64_t period) const {
		return period - this->GetCreationPeriod();
	}
#endif

#if OVERCHARGE_BER
	double InjectionConfigurationLocal::GetBer(const size_t errorCat, uint64_t pastIndex, const uint64_t currentIndex) const {
		const uint64_t markerDif = currentIndex - pastIndex;
		#if MULTIPLE_BER_CONFIGURATION
			if (this->GetBerCount(errorCat) == 1) {
				return this->GetBer(errorCat) * static_cast<double>(markerDif);
			}

			double ber = 0;

			if (markerDif > this->GetBerCount(errorCat)) {
				pastIndex += (markerDif % this->GetBerCount(errorCat));
				const uint64_t start = markerDif + 1;
				const uint64_t final = start + this->GetBerCount(errorCat);
				for (uint64_t i = start; i < final; ++i) {
					ber += (this->GetBer(errorCat, i - start + pastIndex) * static_cast<double>(i / this->GetBerCount(errorCat)));
				}
			} else {
				for (; pastIndex < currentIndex; ++pastIndex) {
					ber += this->GetBer(errorCat, pastIndex);
				}
			}

			return ber;
		#else
			return this->GetBer(errorCat) * static_cast<double>(markerDif);
		#endif
	}
#endif