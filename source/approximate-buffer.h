#ifndef APPROXIMATE_BUFFER_H
#define APPROXIMATE_BUFFER_H

class FaultInjector;

#include "pin.H"

#include <string>
#include <set>
#include <map>
#include <stdlib.h> 
#include <algorithm>
#include <fstream>
#include <iostream>

#include "compiling-options.h"
#include "period-log.h"
#include "injector-configuration.h"
#include "fault-injector.h"
#include "consumption-profile.h"
#include "range.h"

//extern bool g_isGlobalInjectionEnabled;
//extern int g_level;
extern uint64_t g_currentPeriod;

class ApproximateBuffer : public SizedRange {
	protected:
		const int64_t m_id;
		const size_t m_minimumReadBackupSize;
		//uint64_t m_creationPeriod;
		//PIN_LOCK m_bufferLock;
		int32_t m_isActive;

		#if DISTANCE_BASED_FAULT_INJECTOR
			DistanceBasedFaultInjector m_faultInjector;
		#elif GRANULAR_FAULT_INJECTOR
			GranularFaultInjector m_faultInjector;
		#else
			FaultInjector m_faultInjector;
		#endif

		PeriodLog<> m_periodLog;

		typedef std::map<size_t, const std::unique_ptr<PeriodLog<>>> BufferLogs;
		BufferLogs m_bufferLogs;

		#if ENABLE_PASSIVE_INJECTION
			#if !DISTANCE_BASED_FAULT_INJECTOR
				std::unique_ptr<uint64_t[]> m_lastAccessPeriod;
				void UpdateLastAccessPeriod(uint8_t const * const initialAddress, const uint32_t accessSize);
				void UpdateLastAccessPeriod(uint8_t const * const accessedAddress);
				void UpdateLastAccessPeriod(const size_t elementIndex);
			#else
				uint64_t m_lastPassiveInjectionPeriod; 
			#endif

			void ApplyPassiveFault(const size_t elementIndex, uint8_t * const accessedAddress);
			void ApplyPassiveFault(uint8_t * const initialAddress, uint8_t const * const finalAddress);
			void ApplyPassiveFault(uint8_t * const accessedAddress);
			void ApplyAllPassiveErrors();

			#if LOG_FAULTS
				uint64_t* GetPassiveErrorsLogFromIterator(const BufferLogs::const_iterator& it) const;
				void AdvanceBufferLogIterator(BufferLogs::const_iterator& it) const;
			#endif
		#endif

		virtual void InitializeRecordsAndBackups(const uint64_t period);
		virtual void GiveAwayRecordsAndBackups(const bool giveAway);

		void StoreCurrentPeriodLog();
		void CleanLogs();

		uint64_t GetCurrentPassiveBerMarker() const;
		bool GetShouldInject(const size_t errorCat, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) const;
		size_t GetTotalNecessaryReadBackupSize() const;

		virtual void HandleMemoryReadSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryWriteSingleElementUnsafe(uint8_t * const accessedAddress, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;

	public:
		ApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
						  const InjectionConfigurationReference& injectorCfg);

		ApproximateBuffer(const ApproximateBuffer&) = delete;
		ApproximateBuffer(const ApproximateBuffer&&) = delete;

		virtual ~ApproximateBuffer();		

		virtual void BackupReadData(uint8_t* const data IF_COMMA_LSBDROPPED(const bool isLSBDrop = false)) = 0;

		void NextPeriod(const uint64_t period);
		virtual void ReactivateBuffer(const uint64_t creationPeriod);
		virtual bool RetireBuffer(const bool giveAwayRecords) = 0; //return true if it's retired
		virtual void HandleMemoryReadSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryWriteSIMD(uint8_t * const initialAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryReadSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryWriteSingleElementSafe(uint8_t * const accessedAddress, const uint32_t accessSize, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryReadScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		virtual void HandleMemoryWriteScattered(IMULTI_ELEMENT_OPERAND const * const memOpInfo, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) = 0;
		
		int64_t GetConfigurationId() const;
		int64_t GetBufferId() const;

		void WriteLogHeaderToFile(std::ofstream& outputLog, const std::string& basePadding = "") const;
		void WriteAccessLogToFile(std::ofstream& outputLog, std::array<std::array<uint64_t, AccessTypes::Size>, AccessPrecision::Size>& totalTargetAccessesBytes, std::array<uint64_t, ErrorCategory::Size>& totalTargetInjections, const std::string& basePadding = "") const;
		void WriteEnergyLogToFile(std::ofstream& outputLog, std::array<std::array<double, ErrorCategory::Size>, ConsumptionType::Size>& totalTargetEnergy, const ConsumptionProfile& respectiveConsumptionProfile, const std::string& basePadding = "") const;
};

#endif /* APPROXIMATE_BUFFER_H */