//MUST LOCK
#include "tracking-buffer.h"

template <bool isPrecise>
TrackingBuffer<isPrecise>::TrackingBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes, const size_t bitDepth, const int64_t configurationId) :
    SizedRange(bufferRange, dataSizeInBytes),
	m_id(id),
	m_isActive(1),
	m_periodLog(creationPeriod, bitDepth),
	m_bufferLogs() {

	if (bitDepth > (this->m_dataSizeInBytes * BYTE_SIZE)) {
		std::cerr << "ApproxSS Error: Bit Depth (" << bitDepth << "bits in Configuration " << configurationId << ") greater than Data Size (" << (this->m_dataSizeInBytes * BYTE_SIZE) << "bits in Buffer " << this->m_id << ")" << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}

	if (this->m_initialAddress > this->m_finalAddress) {
		std::cerr << "ApproxSS Error: On Buffer " << this->m_id << ".  Initial address (" << ((size_t) this->m_initialAddress) << ") must be less than final address (" << ((size_t) this->m_finalAddress)  << ")" << std::endl; //static_cast<size_t>
		PIN_ExitProcess(EXIT_FAILURE);
	}

	if (this->size() < this->m_dataSizeInBytes) {
		std::cerr << "ApproxSS Error: On Buffer " << this->m_id << ". Buffer Size (" << this->size() << ") must be greater or equal to Data Size (" << this->m_dataSizeInBytes << ")" << std::endl;
		PIN_ExitProcess(EXIT_FAILURE);
	}
}

template <bool isPrecise>
TrackingBuffer<isPrecise>::~TrackingBuffer() {
    this->CleanLogs();
}

template <bool isPrecise>
void TrackingBuffer<isPrecise>::CleanLogs() { //for some reason, just calling .clear will cause a segmentation fault
	for (auto it = this->m_bufferLogs.cbegin(); it != this->m_bufferLogs.cend(); ) {
		it = this->m_bufferLogs.erase(it);
	}
}

template <bool isPrecise>
int64_t TrackingBuffer<isPrecise>::GetBufferId() const {
	return this->m_id;
}

//MUST LOCK
template <bool isPrecise>
void TrackingBuffer<isPrecise>::StoreCurrentPeriodLog() {
	this->m_bufferLogs[this->m_periodLog.m_period] = std::make_unique<PeriodLog<isPrecise>>(this->m_periodLog, this->GetBitDepth());
}

//WAS LOCKED
template <bool isPrecise>
void TrackingBuffer<isPrecise>::NextPeriod(const int64_t period) {
	this->StoreCurrentPeriodLog();
    this->m_periodLog.ResetCounts(period, this->GetBitDepth());
}

template <bool isPrecise>
void TrackingBuffer<isPrecise>::ResetOrRestorePeriodLog(const int64_t period) { //TODO: FIX? THIS IS PROBABLY WRONG, ITS CONSIDERING LINEAR PERIODS I THINK
    const auto it = this->m_bufferLogs.find(period);
	if (it != this->m_bufferLogs.cend()) {
		this->m_periodLog = PeriodLog<isPrecise>(*(it->second.get()), this->GetBitDepth());
		this->m_bufferLogs.erase(it);
	} else {
		this->m_periodLog.ResetCounts(period, this->GetBitDepth());
	}
}

//MUST LOCK
//AND m_isActive MUST BE CHECKED!!!
template <bool isPrecise>
void TrackingBuffer<isPrecise>::ReactivateBuffer(const int64_t creationPeriod) {
    this->ResetOrRestorePeriodLog(creationPeriod);
}

template <bool isPrecise>
void TrackingBuffer<isPrecise>::WriteLogHeaderToFile(std::ofstream& outputLog, const std::string& basePadding /*= ""*/) const {
	const std::string padding = basePadding + '\t';
	outputLog << basePadding << "Buffer {" << std::endl;
	outputLog << padding << "Id: " << this->m_id << std::endl;
	outputLog << padding << "Precision: " << (isPrecise ? "Precise" : "Approximate") << std::endl;
	outputLog << padding << "Initial Address: " << (size_t) this->m_initialAddress << std::endl;	//static_cast<size_t>
	outputLog << padding << "Final Address: " << (size_t) this->m_finalAddress << std::endl;		//static_cast<size_t>
	outputLog << padding << "Configuration Id: " << this->GetConfigurationId() << std::endl;
	outputLog << padding << "Data Size (Bytes): " << this->m_dataSizeInBytes << std::endl;
	outputLog << padding << "Bit Depth: " << this->GetBitDepth() << std::endl;

	outputLog << padding << "Software/Proposed Size Bytes: " << this->GetSoftwareBufferSizeInBytes() << " / " << FormatDouble(CalculateProposedByteSize(this->GetNumberOfElements(), this->GetBitDepth())) << std::endl;
	outputLog << padding << "Elements: " << this->GetNumberOfElements() << std::endl << std::endl;
}

template <bool isPrecise>
void TrackingBuffer<isPrecise>::WriteAccessLogToFile(std::ofstream& outputLog, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& totalTargetAccessesBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding) const {
	const std::string padding = basePadding + '\t';
	
	outputLog << std::endl;
	this->WriteLogHeaderToFile(outputLog, basePadding);

	uint64_t activePeriodsCount	= 0;
	std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size> bufferAccessedBytes;
	std::fill_n(&(bufferAccessedBytes[0][0]), AccessPrecision::Size * AccessTypes::Size, 0);

	const InjectionConfigurationReference& referenceConfiguration = this->GetInjectionConfigurationReference();

	for (const auto& [_, bufLog] : this->m_bufferLogs) {
		++activePeriodsCount;
		bufLog->WriteAccessLogToFile(outputLog, this->GetBitDepth(), this->m_dataSizeInBytes, bufferAccessedBytes, totalTargetInjections, referenceConfiguration, padding);
	}

	if (!IsAccessBufferCountVirgin(bufferAccessedBytes)) {
		outputLog << padding << "Totals {" << std::endl;
		for (size_t i = 0; i < AccessPrecision::Size; ++i) {
			for (size_t j = 0; j < AccessTypes::Size; ++j) {
				if (bufferAccessedBytes[i][j]) {
					WriteAccessedBytesToFile(outputLog, this->GetBitDepth(), this->m_dataSizeInBytes, bufferAccessedBytes[i][j], AccessTypesNames[j], /*"Buffer " +*/ AccessPrecisionNames[i], padding + "\t");
					totalTargetAccessesBytes[i][j] += bufferAccessedBytes[i][j];
				}
			}
		}
		outputLog << padding << "}\n" << std::endl;
	}

	outputLog << padding << "Active Periods: " << activePeriodsCount << std::endl;

	outputLog << basePadding << "}" << std::endl;
}

template <bool isPrecise>
void TrackingBuffer<isPrecise>::WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& totalTargetEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const std::string& basePadding) const {
	const std::string padding = basePadding + '\t';
	
	outputLog << std::endl;
	this->WriteLogHeaderToFile(outputLog, basePadding);

	uint64_t activePeriodsCount	= 0;
	std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size> bufferEnergy;
	std::fill_n(bufferEnergy.data()->data(), ConsumptionType::Size * ErrorCategory::Size, -std::numeric_limits<double>::denorm_min());

	for (const auto& [_, bufLog] : this->m_bufferLogs) {
		++activePeriodsCount;
		bufLog->WriteEnergyLogToFile(outputLog, bufferEnergy, respectiveConsumptionProfile, this->GetBitDepth(), this->m_dataSizeInBytes, this->GetSoftwareBufferSizeInBytes(), padding);
	}

	if (WasEnergySpent(bufferEnergy)) {
		outputLog << padding << "Total {" << std::endl;

		WriteEnergyConsumptionToLogFile(outputLog, bufferEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding + '\t');
		//WriteEnergyConsumptionSavingsToLogFile(outputLog, bufferEnergy, respectiveConsumptionProfile.HasReferenceValues(), true, padding);
		AddEnergyConsumption(totalTargetEnergy, bufferEnergy);

		outputLog << padding << "}\n" << std::endl;
	}

	outputLog << padding << "Active Periods: " << activePeriodsCount << std::endl;

	outputLog << basePadding << "}" << std::endl;
}

template class TrackingBuffer<false>;
template class TrackingBuffer<true>;