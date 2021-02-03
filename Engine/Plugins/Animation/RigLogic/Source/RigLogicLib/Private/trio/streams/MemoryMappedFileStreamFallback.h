// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// *INDENT-OFF*
#if !defined(TRIO_WINDOWS_FILE_MAPPING_AVAILABLE) && !defined(TRIO_MMAP_AVAILABLE)

#include "trio/streams/MemoryMappedFileStream.h"
#include "trio/types/Aliases.h"

#include <pma/TypeDefs.h>
#include <status/Provider.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cstddef>
#include <cstdio>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace trio {

class MemoryMappedFileStreamFallback : public MemoryMappedFileStream {
    public:
        MemoryMappedFileStreamFallback(const char* path_, AccessMode accessMode_, MemoryResource* memRes_);
        ~MemoryMappedFileStreamFallback();

        MemoryMappedFileStreamFallback(const MemoryMappedFileStreamFallback&) = delete;
        MemoryMappedFileStreamFallback& operator=(const MemoryMappedFileStreamFallback&) = delete;

        MemoryMappedFileStreamFallback(MemoryMappedFileStreamFallback&&) = delete;
        MemoryMappedFileStreamFallback& operator=(MemoryMappedFileStreamFallback&&) = delete;

        void open() override;
        void close() override;
        std::size_t tell() override;
        void seek(std::size_t position) override;
        std::size_t size() override;
        void read(char* buffer, std::size_t size) override;
        void write(const char* buffer, std::size_t size) override;
        void flush() override;
        void resize(std::size_t size) override;

        MemoryResource* getMemoryResource();

    private:
        static sc::StatusProvider status;

        std::FILE* stream;
        pma::String<char> path;
        AccessMode accessMode;
        std::size_t fileSize;
        MemoryResource* memRes;
};

}  // namespace trio

#endif
// *INDENT-OFF*
