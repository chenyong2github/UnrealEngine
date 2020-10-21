// Copyright Epic Games, Inc. All Rights Reserved.

// *INDENT-OFF*
#if !defined(TRIO_WINDOWS_FILE_MAPPING_AVAILABLE) && !defined(TRIO_MMAP_AVAILABLE)

#include "trio/streams/MemoryMappedFileStreamFallback.h"

#include "trio/utils/ScopedEnumEx.h"

#include <status/Provider.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdio>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace trio {

static std::size_t getFileSizeFallback(const char* path) {
    FILE* stream = nullptr;
    #if defined(_MSC_VER) && !defined(__clang__)
        fopen_s(&stream, path, "rb");
    #else
        stream = fopen(path, "rb");
    #endif
    std::size_t fileSize{};
    if (stream != nullptr) {
        fseek(stream, 0, SEEK_END);
        fileSize = static_cast<std::size_t>(ftell(stream));
        fclose(stream);
    }
    return fileSize;
}

MemoryMappedFileStreamFallback::MemoryMappedFileStreamFallback(const char* path_, AccessMode accessMode_,
                                                               MemoryResource* memRes_) :
    stream{nullptr},
    path{path_, memRes_},
    accessMode{accessMode_},
    fileSize{getFileSizeFallback(path_)},
    memRes{memRes_} {
}

MemoryMappedFileStreamFallback::~MemoryMappedFileStreamFallback() {
    MemoryMappedFileStreamFallback::close();
}

void MemoryMappedFileStreamFallback::open() {
    status.reset();
    if (stream != nullptr) {
        status.set(AlreadyOpenError, path.c_str());
        return;
    }

    const char* openMode = "";
    if (accessMode == AccessMode::ReadWrite) {
        openMode = "r+b";
    } else {
        openMode = (contains(accessMode, AccessMode::Write) ? "wb" : "rb");
    }

    #if defined(_MSC_VER) && !defined(__clang__)
        fopen_s(&stream, path.c_str(), openMode);
    #else
        stream = fopen(path.c_str(), openMode);
    #endif
    if (stream == nullptr) {
        status.set(OpenError, path.c_str());
        return;
    }
    setvbuf(stream, nullptr, _IONBF, 0ul);

    fseek(stream, 0, SEEK_END);
    fileSize = static_cast<std::size_t>(ftell(stream));
    rewind(stream);
}

void MemoryMappedFileStreamFallback::close() {
    fclose(stream);
}

std::size_t MemoryMappedFileStreamFallback::tell() {
    return static_cast<std::size_t>(ftell(stream));
}

void MemoryMappedFileStreamFallback::seek(std::size_t position) {
    fseek(stream, static_cast<int>(position), SEEK_SET);
}

void MemoryMappedFileStreamFallback::read(char* buffer, std::size_t size) {
    const auto bytesRead = fread(buffer, 1ul, size, stream);
    if ((bytesRead != size) && ferror(stream)) {
        status.set(ReadError, path.c_str());
    }
}

void MemoryMappedFileStreamFallback::write(const char* buffer, std::size_t size) {
    const auto bytesWritten = fwrite(buffer, 1ul, size, stream);
    if ((bytesWritten != size) || ferror(stream)) {
        status.set(WriteError, path.c_str());
    }
}

void MemoryMappedFileStreamFallback::flush() {
    // Unbuffered, so noop
}

void MemoryMappedFileStreamFallback::resize(std::size_t  /*unused*/) {
    // No-op, as it's written to disk directly
}

std::size_t MemoryMappedFileStreamFallback::size() {
    return fileSize;
}

MemoryResource* MemoryMappedFileStreamFallback::getMemoryResource() {
    return memRes;
}

}  // namespace trio

#endif
// *INDENT-OFF*
