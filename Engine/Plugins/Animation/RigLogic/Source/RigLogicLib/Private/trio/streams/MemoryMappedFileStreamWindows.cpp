// Copyright Epic Games, Inc. All Rights Reserved.

// *INDENT-OFF*
#ifdef TRIO_WINDOWS_FILE_MAPPING_AVAILABLE

#include "trio/streams/MemoryMappedFileStreamWindows.h"

#include "trio/utils/ScopedEnumEx.h"

#include <pma/PolyAllocator.h>
#include <status/Provider.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <algorithm>
#include <cassert>
#include <cstddef>
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
sc::StatusProvider MemoryMappedFileStreamWindows::status{OpenError, ReadError, WriteError, AlreadyOpenError};
#ifdef __clang__
    #pragma clang diagnostic pop
#endif

static std::size_t getFileSizeWindows(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA w32fad;
    if (GetFileAttributesExA(path, GetFileExInfoStandard, &w32fad) == 0) {
        return 0ul;
    }
    LARGE_INTEGER size;
    size.HighPart = static_cast<LONG>(w32fad.nFileSizeHigh);
    size.LowPart = w32fad.nFileSizeLow;
    return static_cast<std::size_t>(size.QuadPart);
}

MemoryMappedFileStreamWindows::MemoryMappedFileStreamWindows(const char* path_, AccessMode accessMode_, MemoryResource* memRes_) :
    file{INVALID_HANDLE_VALUE},
    mapping{nullptr},
    data{nullptr},
    position{},
    path{path_, memRes_},
    accessMode{accessMode_},
    fileSize{getFileSizeWindows(path_)},
    memRes{memRes_} {
}

MemoryMappedFileStreamWindows::~MemoryMappedFileStreamWindows() {
    MemoryMappedFileStreamWindows::close();
}

void MemoryMappedFileStreamWindows::open() {
    status.reset();
    if (file != INVALID_HANDLE_VALUE) {
        status.set(AlreadyOpenError, path.c_str());
        return;
    }

    // Create file handle
    DWORD access{};
    access |= (contains(accessMode, AccessMode::Read) ? GENERIC_READ : access);
    access |= (contains(accessMode, AccessMode::Write) ? GENERIC_WRITE : access);

    // 0 == no sharing in any way
    DWORD sharing{};

    // If the file does not exist, and it's to be opened in write-only mode, the actual mapping
    // will be delayed until the file is resized to a non-zero size
    DWORD creationDisposition{};
    if (accessMode == AccessMode::ReadWrite) {
        creationDisposition = static_cast<DWORD>(OPEN_EXISTING);
    } else {
        creationDisposition = static_cast<DWORD>(contains(accessMode, AccessMode::Write) ? CREATE_NEW : OPEN_EXISTING);
    }

    file = CreateFileA(path.c_str(), access, sharing, nullptr, creationDisposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        status.set(OpenError, path.c_str());
        return;
    }

    // Retrieve file size
    LARGE_INTEGER size{};
    GetFileSizeEx(file, &size);
    fileSize = static_cast<std::size_t>(size.QuadPart);
    // Due to the above mentioned reason, mapping of 0-length files is delayed.
    if (fileSize == 0ul) {
        if (accessMode == AccessMode::Read) {
            // Read-only access to 0-length files is not possible.
            status.set(OpenError, path.c_str());
            close();
        }
        return;
    }

    // Create file mapping
    const auto protect = static_cast<DWORD>(contains(accessMode, AccessMode::Write) ? PAGE_READWRITE : PAGE_READONLY);
    mapping = CreateFileMapping(file, nullptr, protect, 0u, 0u, nullptr);
    if (mapping == nullptr) {
        status.set(OpenError, path.c_str());
        close();
        return;
    }

    // Map a view of the file mapping into the address space
    DWORD desiredAccess{};
    desiredAccess |= (contains(accessMode, AccessMode::Write) ? FILE_MAP_WRITE : desiredAccess);
    desiredAccess |= (contains(accessMode, AccessMode::Read) ? FILE_MAP_READ : desiredAccess);
    data = MapViewOfFile(mapping, desiredAccess, 0u, 0u, 0ul);
    if (data == nullptr) {
        status.set(OpenError, path.c_str());
        close();
        return;
    }

    seek(0ul);
}

void MemoryMappedFileStreamWindows::close() {
    if (data != nullptr) {
        if (!FlushViewOfFile(data, 0ul)) {
            status.set(WriteError, path.c_str());
        }
        UnmapViewOfFile(data);
        data = nullptr;
    }

    if (mapping != nullptr) {
        CloseHandle(mapping);
        mapping = nullptr;
    }

    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
}

std::size_t MemoryMappedFileStreamWindows::tell() {
    return position;
}

void MemoryMappedFileStreamWindows::seek(std::size_t position_) {
    position = position_;
}

void MemoryMappedFileStreamWindows::read(char* buffer, std::size_t size) {
    const std::size_t available = fileSize - position;
    const std::size_t bytesToRead = std::min(size, available);
    std::memcpy(buffer, static_cast<char*>(data) + position, bytesToRead);
    position += bytesToRead;
}

void MemoryMappedFileStreamWindows::write(const char* buffer, std::size_t size) {
    if (position + size > fileSize) {
        resize(position + size);
        if (fileSize != (position + size)) {
            return;
        }
    }
    std::memcpy(static_cast<char*>(data) + position, buffer, size);
    position += size;
}

void MemoryMappedFileStreamWindows::flush() {
    if (data != nullptr) {
        if (!FlushViewOfFile(data, 0ul)) {
            status.set(WriteError, path.c_str());
        }
    }
}

void MemoryMappedFileStreamWindows::resize(std::size_t size) {
    if (data != nullptr) {
        UnmapViewOfFile(data);
        CloseHandle(mapping);
    }

    // Seek to the new size
    LARGE_INTEGER moveBy{};
    moveBy.QuadPart = static_cast<decltype(moveBy.QuadPart)>(size);
    if (SetFilePointerEx(file, moveBy, nullptr, FILE_BEGIN) == 0) {
        status.set(WriteError, path.c_str());
        return;
    }

    // Resize the file to it's current position
    if (SetEndOfFile(file) == 0) {
        status.set(WriteError, path.c_str());
        return;
    }

    // Recreate the file mapping of the resized file
    mapping = CreateFileMapping(file, nullptr, PAGE_READWRITE, 0u, 0u, nullptr);
    if (mapping == nullptr) {
        status.set(WriteError, path.c_str());
        return;
    }

    // Map a view of the file mapping into the address space
    DWORD desiredAccess{};
    desiredAccess |= (contains(accessMode, AccessMode::Write) ? FILE_MAP_WRITE : desiredAccess);
    desiredAccess |= (contains(accessMode, AccessMode::Read) ? FILE_MAP_READ : desiredAccess);
    data = MapViewOfFile(mapping, desiredAccess, 0u, 0u, 0ul);
    if (data == nullptr) {
        status.set(WriteError, path.c_str());
        return;
    }

    fileSize = size;
}

std::size_t MemoryMappedFileStreamWindows::size() {
    return fileSize;
}

MemoryResource* MemoryMappedFileStreamWindows::getMemoryResource() {
    return memRes;
}

}  // namespace trio

#endif  // TRIO_WINDOWS_FILE_MAPPING_AVAILABLE
// *INDENT-ON*
