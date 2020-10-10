// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// *INDENT-OFF*
#ifdef TRIO_MMAP_AVAILABLE

#include "trio/streams/MemoryMappedFileStream.h"
#include "trio/types/Aliases.h"

#include <pma/TypeDefs.h>
#include <status/Provider.h>

#include <cstddef>

namespace trio {

class MemoryMappedFileStreamUnix : public MemoryMappedFileStream {
    public:
        MemoryMappedFileStreamUnix(const char* path_, AccessMode accessMode_, MemoryResource* memRes_);
        ~MemoryMappedFileStreamUnix();

        MemoryMappedFileStreamUnix(const MemoryMappedFileStreamUnix&) = delete;
        MemoryMappedFileStreamUnix& operator=(const MemoryMappedFileStreamUnix&) = delete;

        MemoryMappedFileStreamUnix(MemoryMappedFileStreamUnix&&) = delete;
        MemoryMappedFileStreamUnix& operator=(MemoryMappedFileStreamUnix&&) = delete;

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

        void* data;
        std::size_t position;
        pma::String<char> path;
        AccessMode accessMode;
        std::size_t fileSize;
        MemoryResource* memRes;
};

}  // namespace trio

#endif  // TRIO_MMAP_AVAILABLE
// *INDENT-ON*
