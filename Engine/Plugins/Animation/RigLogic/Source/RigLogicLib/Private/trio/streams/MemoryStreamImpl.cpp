// Copyright Epic Games, Inc. All Rights Reserved.

#include "trio/streams/MemoryStreamImpl.h"

#include <pma/PolyAllocator.h>
#include <status/Provider.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cassert>
#include <cstddef>
#include <cstring>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace trio {

const sc::StatusCode MemoryStream::ReadError{121, "Error reading from memory stream."};
const sc::StatusCode MemoryStream::WriteError{122, "Error writing to memory stream."};

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
sc::StatusProvider MemoryStreamImpl::status{ReadError, WriteError};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

MemoryStream::~MemoryStream() = default;

MemoryStream* MemoryStream::create(MemoryResource* memRes) {
    return create(0ul, memRes);
}

MemoryStream* MemoryStream::create(std::size_t initialSize, MemoryResource* memRes) {
    pma::PolyAllocator<MemoryStreamImpl> alloc{memRes};
    return alloc.newObject(initialSize, memRes);
}

void MemoryStream::destroy(MemoryStream* instance) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto stream = static_cast<MemoryStreamImpl*>(instance);
    pma::PolyAllocator<MemoryStreamImpl> alloc{stream->getMemoryResource()};
    alloc.deleteObject(stream);
}

MemoryStreamImpl::MemoryStreamImpl(std::size_t initialSize, MemoryResource* memRes_) :
    data{initialSize, static_cast<char>(0), memRes_},
    position{},
    memRes{memRes_} {
}

void MemoryStreamImpl::open() {
    position = 0ul;
}

void MemoryStreamImpl::close() {
    position = 0ul;
}

std::size_t MemoryStreamImpl::tell() {
    return position;
}

void MemoryStreamImpl::seek(std::size_t position_) {
    position = position_;
}

void MemoryStreamImpl::read(char* buffer, std::size_t size) {
    const std::size_t available = data.size() - position;
    const std::size_t bytesToRead = std::min(size, available);
    std::memcpy(buffer, &data[position], bytesToRead);
    position += bytesToRead;
}

void MemoryStreamImpl::write(const char* buffer, std::size_t size) {
    const std::size_t available = data.size() - position;
    if (available < size) {
        data.resize(data.size() + (size - available));
    }
    std::memcpy(&data[position], buffer, size);
    position += size;
}

std::size_t MemoryStreamImpl::size() {
    return data.size();
}

MemoryResource* MemoryStreamImpl::getMemoryResource() {
    return memRes;
}

}  // namespace trio
