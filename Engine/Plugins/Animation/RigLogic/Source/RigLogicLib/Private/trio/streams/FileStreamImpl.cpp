// Copyright Epic Games, Inc. All Rights Reserved.

#include "trio/streams/FileStreamImpl.h"

#include "trio/utils/ScopedEnumEx.h"

#include <pma/PolyAllocator.h>
#include <status/Provider.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cassert>
#include <cstddef>
#include <cstring>
#include <ios>
#include <type_traits>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace trio {

const sc::StatusCode FileStream::OpenError{100, "Error opening file: %s"};
const sc::StatusCode FileStream::ReadError{101, "Error reading file: %s"};
const sc::StatusCode FileStream::WriteError{102, "Error writing file: %s"};
const sc::StatusCode FileStream::AlreadyOpenError{103, "File already open: %s"};

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
sc::StatusProvider FileStreamImpl::status{OpenError, ReadError, WriteError, AlreadyOpenError};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

static std::size_t getFileSizeStd(const char* path) {
    std::streamoff fileSize = std::ifstream(path, std::ios_base::ate | std::ios_base::binary).tellg();
    return (fileSize > 0 ? static_cast<std::size_t>(fileSize) : 0ul);
}

FileStream::~FileStream() = default;

FileStream* FileStream::create(const char* path, AccessMode accessMode, OpenMode openMode, MemoryResource* memRes) {
    pma::PolyAllocator<FileStreamImpl> alloc{memRes};
    return alloc.newObject(path, accessMode, openMode, memRes);
}

void FileStream::destroy(FileStream* instance) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    auto stream = static_cast<FileStreamImpl*>(instance);
    pma::PolyAllocator<FileStreamImpl> alloc{stream->getMemoryResource()};
    alloc.deleteObject(stream);
}

FileStreamImpl::FileStreamImpl(const char* path_, AccessMode accessMode_, OpenMode openMode_, MemoryResource* memRes_) :
    path{path_, memRes_},
    accessMode{accessMode_},
    openMode{openMode_},
    fileSize{getFileSizeStd(path_)},
    memRes{memRes_} {
}

void FileStreamImpl::open() {
    status.reset();
    if (file.is_open()) {
        status.set(AlreadyOpenError, path.c_str());
        return;
    }

    std::ios_base::openmode flags{};
    flags |= (contains(accessMode, AccessMode::Read) ? std::ios_base::in : flags);
    flags |= (contains(accessMode, AccessMode::Write) ? std::ios_base::out : flags);
    flags |= (contains(openMode, OpenMode::Binary) ? std::ios_base::binary : flags);
    flags |= std::ios_base::ate;

    file.open(path.c_str(), flags);
    if (!file.good()) {
        status.set(OpenError, path.c_str());
        return;
    }
    fileSize = static_cast<std::size_t>(file.tellg());
    seek(0ul);
}

void FileStreamImpl::close() {
    file.close();
}

std::size_t FileStreamImpl::tell() {
    return static_cast<std::size_t>(file.tellp());
}

void FileStreamImpl::seek(std::size_t position) {
    file.seekp(static_cast<std::streamoff>(position));
}

void FileStreamImpl::read(char* buffer, std::size_t size) {
    file.read(buffer, static_cast<std::streamsize>(size));
    if (!file.good() && !file.eof()) {
        status.set(ReadError, path.c_str());
    }
}

void FileStreamImpl::write(const char* buffer, std::size_t size) {
    file.write(buffer, static_cast<std::streamsize>(size));
    if (!file.good()) {
        status.set(WriteError, path.c_str());
    }
}

std::size_t FileStreamImpl::size() {
    return fileSize;
}

MemoryResource* FileStreamImpl::getMemoryResource() {
    return memRes;
}

}  // namespace trio
