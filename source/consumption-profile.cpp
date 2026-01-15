#include "consumption-profile.h"

ConsumptionProfile::ConsumptionProfile(const int64_t configurationId) : m_configurationId(configurationId) {}

int64_t ConsumptionProfile::GetConfigurationId() const {
	return this->m_configurationId;
}

bool ConsumptionProfile::HasReferenceValues() const {
	return this->m_hasReferenceValues;
}

void ConsumptionProfile::SetHasReference(const bool hasReference) {
	this->m_hasReferenceValues = hasReference;
}

void ConsumptionProfile::StringfyConsumptionValues(std::string& s, const std::string& lineStart, const size_t consumptionType) const {
	for (size_t errorCat = 0; errorCat < ErrorCategory::Size; ++errorCat) {
		s += lineStart + ErrorCategoryNames[errorCat] + "Consumption:";

		#if MULTIPLE_BER_CONFIGURATION
			for (size_t valueIndex = 0; valueIndex < this->m_consumptionValuesCount[errorCat]; ++valueIndex) {
				s += " " + std::to_string(this->m_consumptionValues[consumptionType][errorCat][valueIndex]) + "pJ;";
			}
		#else
			s += " " + std::to_string(this->m_consumptionValues[consumptionType][errorCat]) + "pJ;";
		#endif

		s += "\n";
	}
}


std::string ConsumptionProfile::toString(const std::string& lineStart /*= ""*/) const {
	std::string s = "";
	s += lineStart + "ConfigurationId: " + std::to_string(this->m_configurationId) + "\n";

	if (this->HasReferenceValues()) {
		s += lineStart + "REFERENCE_VALUES\n";
		this->StringfyConsumptionValues(s, lineStart, ConsumptionType::Reference);
	} else {
		s += lineStart + "NO_REFERENCE_VALUES\n";
	}

	s += lineStart + "APPROXIMATE_VALUES\n";
	this->StringfyConsumptionValues(s, lineStart, ConsumptionType::Approximate);

	return s;
}

double ConsumptionProfile::EstimateEnergyConsumption(const size_t softwareProcessedBytes, const size_t bitDepth, const size_t dataSizeInBytes, const size_t consumptionTypeIndex, const size_t errorCat, const size_t berIndex /*= 0*/) const {
	const double proposedProcessedBytes = (static_cast<double>(softwareProcessedBytes) / static_cast<double>(dataSizeInBytes)) * (static_cast<double>(bitDepth)) / static_cast<double>(BYTE_SIZE);
	
	#if MULTIPLE_BER_CONFIGURATION
		const double byteConsumptionValue = this->m_consumptionValues[consumptionTypeIndex][errorCat][berIndex];
	#else
		const double byteConsumptionValue = this->m_consumptionValues[consumptionTypeIndex][errorCat];
	#endif

	return proposedProcessedBytes * byteConsumptionValue;
}

#if MULTIPLE_BER_CONFIGURATION
	void ConsumptionProfile::SetConsumptionValue(const size_t consumptionType, const size_t errorCat, const size_t index, const double consumptionValue) {
		this->m_consumptionValues[consumptionType][errorCat][index]	= consumptionValue;
	}

	void ConsumptionProfile::SetConsumptionValueCount(const size_t consumptionType, const size_t errorCat, const size_t count) {
		this->m_consumptionValuesCount[errorCat] = count;
		this->m_consumptionValues[consumptionType][errorCat] = std::make_unique<double[]>(count);
	}
#else
	void ConsumptionProfile::SetConsumptionValue(const size_t consumptionType, const size_t errorCat, const double consumptionValue) {
		this->m_consumptionValues[consumptionType][errorCat]		= consumptionValue;
	}

	double ConsumptionProfile::GetConsumptionValue(const size_t consumptionType, const size_t errorCat) const {
		return this->m_consumptionValues[consumptionType][errorCat];
	}
#endif
