#include "injector-configuration.h"


#if MULTIPLE_BERS
	MultiBer::MultiBer() {
		this->values = std::unique_ptr<ErrorType[]>((ErrorType*) std::malloc(sizeof(ErrorType)));
		this->values[0] = InjectorConfiguration::GetZeroBerValue();
		this->count = 1;
	}
#endif

#include <iostream>

InjectorConfiguration::InjectorConfiguration() {
	this->m_configurationId = 0;
	this->m_bitDepth = 8;

	#if MULTIPLE_BERS
		this->m_creationPeriod = g_currentPeriod;

		this->m_bersArray = std::shared_ptr<std::array<MultiBer, ErrorCategory::Size>>((std::array<MultiBer, ErrorCategory::Size>*) std::malloc(sizeof(std::array<MultiBer, ErrorCategory::Size>)));

		for (size_t i = 0; i < ErrorCategory::Size; i++) {
			(*this->m_bersArray)[i] = MultiBer();
			this->ReviseBer(i);
		}
	#else
		for (size_t i = 0; i < ErrorCategory::Size; i++) {
			this->SetBer(i, InjectorConfiguration::GetZeroBerValue());
		}
	#endif
}

int64_t InjectorConfiguration::GetConfigurationId() const {
	return this->m_configurationId;
}

size_t InjectorConfiguration::GetBitDepth() const {
	return this->m_bitDepth;
}

void InjectorConfiguration::SetBer(const size_t errorCat, const ErrorType ber) {
	this->m_bers[errorCat] = ber;
	this->ReviseShouldGoOn(errorCat);
}

void InjectorConfiguration::SetBitDepth(const size_t bitDepth) {
	this->m_bitDepth = bitDepth;
}

void InjectorConfiguration::SetConfigurationId(const int64_t id) {
	this->m_configurationId = id;
}

#if MULTIPLE_BERS
	void InjectorConfiguration::AdvanceBerIndex() {
		this->ReviseBers();
	}

	void InjectorConfiguration::ResetBerIndex(const uint64_t newCreationPeriod) {
		this->m_creationPeriod = newCreationPeriod;
		this->ReviseBers();
	}

	void InjectorConfiguration::ReviseBers() {
		for (size_t i = 0; i < ErrorCategory::Size; i++) {
			this->ReviseBer(i);
		}
	}

	void InjectorConfiguration::ReviseBer(const size_t errorCat) {
		this->SetBer(errorCat, this->GetBer(errorCat, this->GetBerIndex()));
	}

	void InjectorConfiguration::SetBer(const size_t errorCat, const size_t index, const ErrorType ber) {
		(*this->m_bersArray)[errorCat].values[index] = ber;
		this->ReviseBer(errorCat);
	}

	void InjectorConfiguration::SetBerCount(const size_t errorCat, const size_t count) {
		(*this->m_bersArray)[errorCat].count = count;
		(*this->m_bersArray)[errorCat].values = std::unique_ptr<ErrorType[]>(new ErrorType[count]);
	}

	uint64_t InjectorConfiguration::GetBerCurrentIndex(const size_t errorCat) const {
		return (this->GetBerIndex() % this->GetBerCount(errorCat));
	}

	ErrorType InjectorConfiguration::GetBer(const size_t errorCat, const size_t index) const {
		return (*this->m_bersArray)[errorCat].values[index % this->GetBerCount(errorCat)];
	}

	size_t InjectorConfiguration::GetBerCount(const size_t errorCat) const {
		return (*this->m_bersArray)[errorCat].count;
	}

	uint64_t InjectorConfiguration::GetCreationPeriod() const {
		return this->m_creationPeriod;
	}

	uint64_t InjectorConfiguration::GetBerIndex() const {
		return g_currentPeriod - this->GetCreationPeriod();
	}

	uint64_t InjectorConfiguration::GetBerIndexFromPeriod(const uint64_t period) const {
		return period - this->GetCreationPeriod();
	}

	std::string InjectorConfiguration::MultipleBersToString(const MultiBer& ber) {
		std::stringstream s;
		for (size_t i = 0; i < ber.count; ++i) {
			s << " " << InjectorConfiguration::SingleBerToString(ber.values[i]);
		}

		return s.str();
	}

	InjectorConfiguration::InjectorConfiguration(const InjectorConfiguration& other) {
		*this = other;
	}

	InjectorConfiguration& InjectorConfiguration::operator=(const InjectorConfiguration& other) {
		if (this != &other)	{
			this->m_configurationId	= other.m_configurationId;
			this->m_bitDepth 		= other.m_bitDepth;
			this->m_bersArray		= other.m_bersArray;

			this->ResetBerIndex(g_currentPeriod);
		}

		return *this;
	}
#endif

#if CHOSEN_FAULT_INJECTOR != DISTANCE_BASED_FAULT_INJECTOR
	double InjectorConfiguration::GetBer(const size_t errorCat, uint64_t pastIndex, const uint64_t currentIndex) const {
		const uint64_t markerDif = currentIndex - pastIndex;
		#if MULTIPLE_BERSs
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

ErrorType InjectorConfiguration::GetZeroBerValue() {
	#if CHOSEN_FAULT_INJECTOR == DISTANCE_BASED_FAULT_INJECTOR
		return {0, 0};
	#else
		return 0;
	#endif
}

bool InjectorConfiguration::ShouldGoOn(const std::pair<double, double>& ber) {
	return !(ber.first == 0 && ber.second == 0);
}

bool InjectorConfiguration::ShouldGoOn(const double ber) {
	return ber != 0;
}

void InjectorConfiguration::ReviseShouldGoOn(const size_t errorCat) {
	#if MULTIPLE_BERS && ENABLE_PASSIVE_INJECTION
		this->m_shouldGoOn[errorCat] = this->ShouldGoOn(this->GetBer(errorCat)) || (errorCat == ErrorCategory::Passive && this->GetBerCount(errorCat) > 1);
	#else
		this->m_shouldGoOn[errorCat] = this->ShouldGoOn(this->GetBer(errorCat));
	#endif
}

bool InjectorConfiguration::GetShouldGoOn(const size_t errorCat) const {
	return this->m_shouldGoOn[errorCat];
}

ErrorType InjectorConfiguration::GetBer(const size_t errorCat) const{
	return this->m_bers[errorCat];
}

std::string InjectorConfiguration::SingleBerToString(const std::pair<double, double>& ber) {
	return std::to_string(ber.first) + " " + std::to_string(ber.second) + ";"; 
}

std::string InjectorConfiguration::SingleBerToString(const double ber) {
	return std::to_string(ber) + ";"; 
}

std::string InjectorConfiguration::toString(const std::string& lineStart /*= ""*/) const {
	std::string s = "";
	s += lineStart + "ConfigurationId: "	+ std::to_string(this->m_configurationId)	+ "\n";
	s += lineStart + "BitDepth: "			+ std::to_string(this->m_bitDepth)			+ "\n";

	for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
		s += lineStart + ErrorCategoryNames[errorCat] + "Ber: " +
		#if MULTIPLE_BERS
			InjectorConfiguration::MultipleBersToString((*this->m_bersArray)[errorCat])
		#else
			InjectorConfiguration::SingleBerToString(this->m_bers[errorCat])
		#endif
		+ "\n";
	}

	return s;
}