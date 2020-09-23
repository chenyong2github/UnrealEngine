// Copyright Epic Games, Inc. All Rights Reserved.

// *INDENT-OFF*
#ifdef TRIO_MMAP_AVAILABLE

#include "trio/streams/MemoryMappedFileStreamUnix.h"
#include "trio/utils/ScopedEnumEx.h"

#include <pma/PolyAllocator.h>
#include <status/Provider.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ios>
#include <type_traits>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace trio {

#ifdef __clang__
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wglobal-constructors"
#endif
sc::StatusProvider MemoryMappedFileStreamUnix::status{OpenError, ReadError, WriteError, AlreadyOpenError};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

static std::size_t getFileSizeUnix(const char* path) {
    struct stat st{};
    if (::stat(path, &st) != 0) {
        return 0ul;
    }
    return static_cast<std::size_t>(st.st_size);
}

MemoryMappedFileStreamUnix::MemoryMappedFileStreamUnix(const char* path_, AccessMode accessMode_, MemoryResource* memRes_) :
    data{nullptr},
    position{},
    path{path_, memRes_},
    accessMode{accessMode_},
    fileSize{getFileSizeUnix(path_)},
    memRes{memRes_} {
}

MemoryMappedFileStreamUnix::~MemoryMappedFileStreamUnix() {
    MemoryMappedFileStreamUnix::close();
}

void MemoryMappedFileStreamUnix::open() {
    status.reset();
    if (data != nullptr) {
        status.set(AlreadyOpenError, path.c_str());
        return;
    }

    int openFlags{};
    if (accessMode == AccessMode::ReadWrite) {
        openFlags = O_RDWR;
    } else if (accessMode == AccessMode::Read) {
        openFlags = O_RDONLY;
    } else if (accessMode == AccessMode::Write) {
        openFlags = O_WRONLY | O_CREAT;
    }

    const int mode = (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    int fd = ::open(path.c_str(), openFlags, mode);
    if (fd == -1) {
        status.set(OpenError, path.c_str());
        return;
    }

    struct stat st{};
    if (::fstat(fd, &st) == 0) {
        fileSize = static_cast<std::size_t>(st.st_size);
    } else {
        fileSize = 0ul;
        status.set(OpenError, path.c_str());
        ::close(fd);
        return;
    }

    int prot{};
    prot |= (contains(accessMode, AccessMode::Write) ? PROT_WRITE : prot);
    prot |= (contains(accessMode, AccessMode::Read) ? PROT_READ : prot);

    const int flags = (accessMode == AccessMode::Read ? MAP_PRIVATE : MAP_SHARED);

    data = ::mmap(nullptr, fileSize, prot, flags, fd, 0);
    if (data == reinterpret_cast<void*>(-1)) {
        status.set(OpenError, path.c_str());
        ::close(fd);
        data = nullptr;
        return;
    }

    ::close(fd);
    MemoryMappedFileStreamUnix::seek(0ul);
}

void MemoryMappedFileStreamUnix::close() {
    if (data != nullptr) {
        const bool readOnly = (accessMode == AccessMode::Read);
        if (!readOnly && ::msync(data, fileSize, MS_SYNC) != 0) {
            status.set(WriteError, path.c_str());
        }
        ::munmap(data, fileSize);
        data = nullptr;
    }
}

std::size_t MemoryMappedFileStreamUnix::tell() {
    return position;
}

void MemoryMappedFileStreamUnix::seek(std::size_t position_) {
    position = position_;
}

void MemoryMappedFileStreamUnix::read(char* buffer, std::size_t size) {
    const std::size_t available = fileSize - position;
    const std::size_t bytesToRead = std::min(size, available);
    std::memcpy(buffer, static_cast<char*>(data) + position, bytesToRead);
    position += bytesToRead;
}

void MemoryMappedFileStreamUnix::write(const char* buffer, std::size_t size) {
    if (position + size > fileSize) {
        resize(position + size);
        if (fileSize != (position + size)) {
            return;
        }
    }
    std::memcpy(data, buffer + position, size);
    position += size;
}

void MemoryMappedFileStreamUnix::flush() {
    if (data != nullptr) {
        if (::msync(data, fileSize, MS_SYNC) != 0) {
            status.set(WriteError, path.c_str());
        }
    }
}

void MemoryMappedFileStreamUnix::resize(std::size_t size) {
    #ifdef TRIO_MREMAP_AVAILABLE
    auto remapped = ::mremap(data, fileSize, size, MREMAP_MAYMOVE);
    if (remapped == reinterpret_cast<void*>(-1)) {
        status.set(WriteError, path.c_str());
        return;
    }
    data = remapped;
    fileSize = size;
    #else
    if (::msync(data, fileSize, MS_SYNC) != 0) {
        status.set(WriteError, path.c_str());
        return;
    }
    ::munmap(data, fileSize);
    if (::truncate(path.c_str(), size) != 0) {
        status.set(WriteError, path.c_str());
        return;
    }
    open();
    #endif  // TRIO_MREMAP_AVAILABLE
}

std::size_t MemoryMappedFileStreamUnix::size() {
    return fileSize;
}

MemoryResource* MemoryMappedFileStreamUnix::getMemoryResource() {
    return memRes;
}

}  // namespace trio

#endif  // TRIO_MMAP_AVAILABLE
// *INDENT-ON*
