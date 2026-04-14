#ifndef RANGE_H
#define RANGE_H

#include <cstdint>
#include <string>

class Range {
	public:
		uint8_t* const m_initialAddress;
		uint8_t const * const m_finalAddress;

		Range(uint8_t * const initialAddress, uint8_t const * const finalAddress);

		ssize_t ssize() const;
		size_t size() const;
		bool IsEqual(const Range& other) const;
		bool DoesIntersectWith(uint8_t const * const address) const;
		bool DoesIntersectWith(const Range& other) const;

		friend bool operator<(const Range& lhv, const Range& rhv);
};

class SizedRange : public Range {
	protected:
    	const size_t m_dataSizeInBytes;

	public:
		SizedRange(const Range& range, const size_t dataSizeInBytes);

		size_t GetSoftwareBufferSizeInBytes() const;
		ssize_t GetSoftwareBufferSSizeInBytes() const;
		size_t GetNumberOfElements() const;
		size_t GetIndexFromAddress(uint8_t const * const address) const;
		uint8_t* GetAddressFromIndex(const size_t elementIndex) const;
		size_t GetAlignmentOffset(uint8_t const * const address) const;
		bool IsMisaligned(uint8_t const * const address) const;
		bool IsIgnorableMisaligned(uint8_t const * const address, const uint32_t accessSize) const;
};

#endif /* RANGE_H */