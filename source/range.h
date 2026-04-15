#ifndef RANGE_H
#define RANGE_H

#include <cstdint>
#include <string>

// tried to split into .h and .cpp but lost 14% performance due to poor inlining

class Range {
    public:
        uint8_t* const m_initialAddress;
        uint8_t const * const m_finalAddress;

        Range(uint8_t * const initialAddress, uint8_t const * const finalAddress) : m_initialAddress(initialAddress), m_finalAddress(finalAddress) {}

        ssize_t ssize() const {
            return (this->m_finalAddress + 1) - this->m_initialAddress;
        }

        size_t size() const {
            return static_cast<size_t>(this->ssize());
        }

        bool IsEqual(const Range& other) const {
            return this->m_initialAddress == other.m_initialAddress && this->m_finalAddress == other.m_finalAddress;
        }

        bool DoesIntersectWith(uint8_t const * const address) const {
            return (address >= this->m_initialAddress && address <= this->m_finalAddress);
        }

        bool DoesIntersectWith(const Range& other) const {
            return (this->m_initialAddress <= other.m_finalAddress && this->m_finalAddress >= other.m_initialAddress);
        }

        friend bool operator<(const Range& lhv, const Range& rhv) {  
            return lhv.m_finalAddress < rhv.m_initialAddress; //m_finalAddress is not included
        } 
};

class SizedRange : public Range {
	protected:
    	const size_t m_dataSizeInBytes;

	public:
		SizedRange(const Range& range, const size_t dataSizeInBytes): Range(range), m_dataSizeInBytes(dataSizeInBytes) {}

		size_t GetSoftwareBufferSizeInBytes() const {
			return this->size();
		}

		ssize_t GetSoftwareBufferSSizeInBytes() const {
			return this->ssize();
		}

		size_t GetNumberOfElements() const {
			return this->GetSoftwareBufferSizeInBytes() / this->m_dataSizeInBytes;
		}

		size_t GetIndexFromAddress(uint8_t const * const address) const {
			return ((size_t) (address - this->m_initialAddress)) / this->m_dataSizeInBytes; //static_cast<size_t>
		}

		uint8_t* GetAddressFromIndex(const size_t elementIndex) const {
			return this->m_initialAddress + (elementIndex * this->m_dataSizeInBytes);
		}

		size_t GetAlignmentOffset(uint8_t const * const address) const {
			return static_cast<size_t>(address - this->m_initialAddress) % this->m_dataSizeInBytes;
		}

		bool IsMisaligned(uint8_t const * const address) const {
			return this->GetAlignmentOffset(address) != 0; 
		}
		bool IsIgnorableMisaligned(uint8_t const * const address, const uint32_t accessSize) const {
			return this->IsMisaligned(address) && accessSize < this->m_dataSizeInBytes;
		}
};

#endif /* RANGE_H */