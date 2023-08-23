#include "fault-injector.h"

std::default_random_engine FaultInjector::m_generator{std::random_device{}()};
std::uniform_real_distribution<double> FaultInjector::m_occurrenceDistribution{0.0f, 1.0f};

FaultInjector::FaultInjector(const InjectorConfiguration& injectorCfg) : InjectorConfiguration(injectorCfg) {}

void FaultInjector::InjectFault(uint8_t* const data, const double ber, ApproximateBuffer* const toBackup AND_LOG_PARAMETER) {
	++g_injectionCalls;
	bool isFaultInjected = false;
	
	for (size_t bitCount = 0; bitCount < this->GetBitDepth(); ++bitCount) {
		const double randomProbability = FaultInjector::m_occurrenceDistribution(FaultInjector::m_generator);

		if (randomProbability < ber) {
			if (toBackup && !isFaultInjected) {
				toBackup->BackupReadData(data);
				isFaultInjected = true;
			}

			const uint8_t faultMask = FaultInjector::m_bitMask << (bitCount % BYTE_SIZE);
			data[bitCount/BYTE_SIZE] ^= faultMask;

			#if LOG_FAULTS
				++injectedByBit[bitCount];
			#endif
		}
	}
}

#if ENABLE_PASSIVE_INJECTION && OVERCHARGE_FLIP_BACK
	void FaultInjector::InjectFaultOvercharged(uint8_t* const data, double ber AND_LOG_PARAMETER) {
		++g_injectionCalls;

		#if LOG_FAULTS
			const uint64_t times = static_cast<uint64_t>(std::floor(ber));
			for (size_t i = 0; i < this->GetBitDepth(); ++i) {
				injectedByBit[i] += times;
			}
		#endif

		ber = std::fmod(ber, 2);

		if (ber >= 1) {			
			for (ssize_t toFlip = static_cast<ssize_t>(this->GetBitDepth()); toFlip > 0; toFlip -= BYTE_SIZE) {
				const uint8_t faultMask = static_cast<uint8_t>((0x01 << (static_cast<size_t>(toFlip) % BYTE_SIZE)) - 1);
				const size_t index = (this->GetBitDepth() - static_cast<size_t>(toFlip)) / BYTE_SIZE;

				data[index] ^= faultMask;
			}			
			
			ber -= 1;

			if (ber == 0) {
				return;
			}
		}

		this->InjectFault(data, ber, nullptr AND_LOG_ARGUMENT(injectedByBit));
	}
#endif


GranularFaultInjector::GranularFaultInjector(const InjectorConfiguration& injectorCfg) : FaultInjector(injectorCfg) {
	this->m_instanceDistribution = std::uniform_int_distribution<size_t>(0, this->GetBitDepth() - 1);
}

void GranularFaultInjector::InjectFault(uint8_t* const data, const double ber, ApproximateBuffer* const toBackup AND_LOG_PARAMETER) {
	++g_injectionCalls;
	bool isFaultInjected = false;
	
	const double randomProbability = m_occurrenceDistribution(FaultInjector::m_generator);

	if (randomProbability < (ber * static_cast<double>(this->GetBitDepth()))) {
		const size_t instanceIndex = this->m_instanceDistribution(FaultInjector::m_generator);
		const uint8_t faultMask = FaultInjector::m_bitMask << (instanceIndex % BYTE_SIZE);

		if (toBackup && !isFaultInjected) {
			toBackup->BackupReadData(data);
			isFaultInjected = true;
		}

		data[instanceIndex/BYTE_SIZE] ^= faultMask;

		#if LOG_FAULTS
			++injectedByBit[instanceIndex];
		#endif
	}
}

#if ENABLE_PASSIVE_INJECTION && OVERCHARGE_FLIP_BACK
	void GranularFaultInjector::InjectFaultOvercharged(uint8_t* const data, double ber AND_LOG_PARAMETER) {
		++g_injectionCalls;

		for (/**/; ber * static_cast<double>(this->GetBitDepth()) > 1; --ber) {
			const size_t instanceIndex = m_instanceDistribution(FaultInjector::m_generator);
			const uint8_t faultMask = FaultInjector::m_bitMask << (instanceIndex % BYTE_SIZE);

			data[instanceIndex/BYTE_SIZE] ^= faultMask;

			#if LOG_FAULTS
				++injectedByBit[instanceIndex];
			#endif
		}

		this->InjectFault(data, ber, nullptr AND_LOG_ARGUMENT(injectedByBit));
	}
#endif


#if CHOSEN_FAULT_INJECTOR == DISTANCE_BASED_FAULT_INJECTOR
	DistanceBasedInjectorRecord::DistanceBasedInjectorRecord(){}

	DistanceBasedInjectorRecord::DistanceBasedInjectorRecord(const std::pair<double, double>& meanAndDev, const size_t dataSizeInBytes, const size_t bitDepth) : m_errorDistanceDistribution(meanAndDev.first, meanAndDev.second) {
		if (!InjectorConfiguration::ShouldGoOn(meanAndDev)) {
			this->m_nextErrorDistance = std::numeric_limits<int64_t>::max();
		} else {
			this->m_nextErrorDistance = 0;
			this->UpdateErrorDistanceAndInjectionBit(dataSizeInBytes, bitDepth);
		}
	}

	int64_t DistanceBasedInjectorRecord::GenerateNewNextErrorDistance() {
		return static_cast<int64_t>(std::abs(this->m_errorDistanceDistribution(FaultInjector::m_generator)));
	}

	bool DistanceBasedInjectorRecord::IsEnabled() const {
		return this->m_nextErrorDistance != std::numeric_limits<int64_t>::max();
	}

	void DistanceBasedInjectorRecord::UpdateErrorDistanceAndInjectionBit(const size_t dataSizeInBytes, const size_t bitDepth) {
		const int64_t nextErrorDistanceInBits = ((this->m_nextErrorDistance/static_cast<int64_t>(dataSizeInBytes)) * static_cast<int64_t>(bitDepth)) + this->GenerateNewNextErrorDistance();
		
		this->m_nextErrorDistance = (nextErrorDistanceInBits/static_cast<int64_t>(bitDepth)) * static_cast<int64_t>(dataSizeInBytes);
		this->m_injectionBit = static_cast<size_t>(std::abs(nextErrorDistanceInBits % static_cast<int64_t>(bitDepth)));
	}

	DistanceBasedFaultInjector::DistanceBasedFaultInjector(const InjectorConfiguration& injectorCfg, const size_t dataSizeInBytes) : FaultInjector(injectorCfg) , m_dataSizeInBytes(dataSizeInBytes) {
		#if MULTIPLE_BERS
			for (size_t i = 0; i < ErrorCategory::Size; ++i) {
				this->m_recordArray[i] = std::unique_ptr<DistanceBasedInjectorRecord[]>((DistanceBasedInjectorRecord*) std::malloc(sizeof(DistanceBasedInjectorRecord) * injectorCfg.GetBerCount(i)));

				for (size_t j = 0; j < injectorCfg.GetBerCount(i); ++j) {
					this->m_recordArray[i][j] = DistanceBasedInjectorRecord(injectorCfg.GetBer(i, j), this->m_dataSizeInBytes, this->GetBitDepth());
				}
			}

			this->ReviseRecords();
		#else
			for (size_t i = 0; i < ErrorCategory::Size; ++i) {
				this->m_record[i] = DistanceBasedInjectorRecord(injectorCfg.GetBer(i), this->m_dataSizeInBytes, this->GetBitDepth());
			}
		#endif
	};

	#if MULTIPLE_BERS
		void DistanceBasedFaultInjector::AdvanceBerIndex() {
			this->ReviseRecords();
		}

		void DistanceBasedFaultInjector::ResetBerIndex(const uint64_t newCreationPeriod) {
			InjectorConfiguration::ResetBerIndex(newCreationPeriod);
			this->ReviseRecords();
		}

		void DistanceBasedFaultInjector::ReviseRecord(const size_t errorCat) {
			this->m_record[errorCat] = this->GetInjectorRecord(errorCat, this->GetBerCurrentIndex(errorCat));
		}

		void DistanceBasedFaultInjector::ReviseRecords() {
			for (size_t i = 0; i < ErrorCategory::Size; ++i) {
				this->ReviseRecord(i);
			}
		}

		DistanceBasedInjectorRecord* DistanceBasedFaultInjector::GetInjectorRecord(const size_t errorCat, const size_t index) {
			return &(this->m_recordArray[errorCat][index % this->GetBerCount(errorCat)]);
		}

		void DistanceBasedFaultInjector::InjectFault(uint8_t* data, const size_t errorCat, const size_t recordIndex, const ssize_t accessSizeInBytes, ApproximateBuffer* const toBackup AND_LOG_PARAMETER) {
			DistanceBasedInjectorRecord& record = *(this->GetInjectorRecord(errorCat, recordIndex));

			if (!record.IsEnabled()) {
				return;
			}

			this->InjectFault(data, record, accessSizeInBytes, toBackup AND_LOG_ARGUMENT(injectedByBit));
		}
	#endif

	DistanceBasedInjectorRecord* DistanceBasedFaultInjector::GetInjectorRecord(const size_t errorCat) {
		#if MULTIPLE_BERS
			return this->m_record[errorCat];
		#else
			return &this->m_record[errorCat];
		#endif
	}

	void DistanceBasedFaultInjector::InjectFault(uint8_t* data, DistanceBasedInjectorRecord& injectorRecord, ssize_t accessSizeInBytes, ApproximateBuffer* const toBackup AND_LOG_PARAMETER) {
		++g_injectionCalls;
		const uint8_t* lastBackedupReadData = nullptr; 

		int64_t& errorDistance = injectorRecord.m_nextErrorDistance;

		errorDistance -= accessSizeInBytes;

		while (errorDistance < 0) {
			data += (accessSizeInBytes + errorDistance);

			if (toBackup && data != lastBackedupReadData) {
				toBackup->BackupReadData(data);
				lastBackedupReadData = data;
			}

			const size_t injectionBit = injectorRecord.m_injectionBit;

			data[injectionBit/BYTE_SIZE] ^= (FaultInjector::m_bitMask << (injectionBit % BYTE_SIZE));

			#if LOG_FAULTS
				++injectedByBit[injectionBit];
			#endif

			accessSizeInBytes = -errorDistance;
			injectorRecord.UpdateErrorDistanceAndInjectionBit(this->m_dataSizeInBytes, this->GetBitDepth());
		}
	}

	void DistanceBasedFaultInjector::InjectFault(uint8_t* data, const size_t errorCat, const ssize_t accessSizeInBytes, ApproximateBuffer* const toBackup AND_LOG_PARAMETER) {
		DistanceBasedInjectorRecord& record = *(this->GetInjectorRecord(errorCat));

		if (!record.IsEnabled()) {
			return;
		}

		this->InjectFault(data, record, accessSizeInBytes, toBackup AND_LOG_ARGUMENT(injectedByBit));
	}
#endif