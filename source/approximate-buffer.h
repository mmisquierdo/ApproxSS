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
//#include "range.h"
#include "tracking-buffer.h"

//extern bool g_isGlobalInjectionEnabled;
//extern int g_level;
extern uint64_t g_currentPeriod;

class ApproximateBuffer : public TrackingBuffer<false> {
	protected:
		const size_t m_minimumReadBackupSize;
		//uint64_t m_creationPeriod;
		//PIN_LOCK m_bufferLock;

		#if DISTANCE_BASED_FAULT_INJECTOR
			DistanceBasedFaultInjector m_faultInjector;
		#elif GRANULAR_FAULT_INJECTOR
			GranularFaultInjector m_faultInjector;
		#else
			FaultInjector m_faultInjector;
		#endif

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
				uint64_t* GetPassiveErrorsLogFromIterator(const BufferLogs<false>::const_iterator& it) const;
				void AdvanceBufferLogIterator(BufferLogs<false>::const_iterator& it) const;
			#endif
		#endif

		virtual void InitializeRecordsAndBackups(const uint64_t period);
		virtual void GiveAwayRecordsAndBackups(const bool giveAway);

		uint64_t GetCurrentPassiveBerMarker() const;
		bool GetShouldInject(const size_t errorCat, const bool isThreadInjectionEnabled IF_COMMA_PIN_LOCKED(const bool isBufferInThread)) const;
		size_t GetTotalNecessaryReadBackupSize() const;

	public:
		ApproximateBuffer(const Range& bufferRange, const int64_t id, const uint64_t creationPeriod, const size_t dataSizeInBytes,
						  const InjectionConfigurationReference& injectorCfg);

		ApproximateBuffer(const ApproximateBuffer&) = delete;
		ApproximateBuffer(const ApproximateBuffer&&) = delete;

		virtual ~ApproximateBuffer();
		
		virtual int64_t GetConfigurationId() const;
        virtual size_t GetBitDepth() const;

		virtual const InjectionConfigurationReference& GetInjectionConfigurationReference() const;

		virtual void BackupReadData(uint8_t* const data IF_COMMA_LSBDROPPED(const bool isLSBDrop = false)) = 0;

		virtual void NextPeriod(const int64_t period);
		virtual void ReactivateBuffer(const int64_t creationPeriod);
};

#endif /* APPROXIMATE_BUFFER_H */