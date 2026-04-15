#include "range.h"

// ---------------------------------------------------------
// Range Class Implementation
// ---------------------------------------------------------

Range::Range(uint8_t * const initialAddress, uint8_t const * const finalAddress) 
    : m_initialAddress(initialAddress), m_finalAddress(finalAddress) {}

ssize_t Range::ssize() const {
    return (this->m_finalAddress + 1) - this->m_initialAddress;
}

size_t Range::size() const {
    return static_cast<size_t>(this->ssize());
}

bool Range::IsEqual(const Range& other) const {
    return this->m_initialAddress == other.m_initialAddress && this->m_finalAddress == other.m_finalAddress;
}

bool Range::DoesIntersectWith(uint8_t const * const address) const {
    return (address >= this->m_initialAddress && address <= this->m_finalAddress);
}

bool Range::DoesIntersectWith(const Range& other) const {
    return (this->m_initialAddress <= other.m_finalAddress && this->m_finalAddress >= other.m_initialAddress);
}

// Friend function (not a member, so it doesn't use Range::)
bool operator<(const Range& lhv, const Range& rhv) {  
    return lhv.m_finalAddress < rhv.m_initialAddress; // m_finalAddress is not included
} 

// ---------------------------------------------------------
// SizedRange Class Implementation
// ---------------------------------------------------------

SizedRange::SizedRange(const Range& range, const size_t dataSizeInBytes)
    : Range(range), m_dataSizeInBytes(dataSizeInBytes) {}

size_t SizedRange::GetSoftwareBufferSizeInBytes() const {
    return this->size();
}

ssize_t SizedRange::GetSoftwareBufferSSizeInBytes() const {
    return this->ssize();
}

size_t SizedRange::GetNumberOfElements() const {
    return this->GetSoftwareBufferSizeInBytes() / this->m_dataSizeInBytes;
}

size_t SizedRange::GetIndexFromAddress(uint8_t const * const address) const {
    return ((size_t) (address - this->m_initialAddress)) / this->m_dataSizeInBytes; // static_cast<size_t>
}

uint8_t* SizedRange::GetAddressFromIndex(const size_t elementIndex) const {
    return this->m_initialAddress + (elementIndex * this->m_dataSizeInBytes);
}

size_t SizedRange::GetAlignmentOffset(uint8_t const * const address) const {
    return static_cast<size_t>(address - this->m_initialAddress) % this->m_dataSizeInBytes;
}

bool SizedRange::IsMisaligned(uint8_t const * const address) const {
    return this->GetAlignmentOffset(address) != 0; 
}

bool SizedRange::IsIgnorableMisaligned(uint8_t const * const address, const uint32_t accessSize) const {
    return this->IsMisaligned(address) && accessSize < this->m_dataSizeInBytes;
}