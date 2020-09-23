// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "trio/streams/FileStream.h"
#include "trio/types/Aliases.h"

#include <pma/TypeDefs.h>
#include <status/Provider.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cstddef>
#include <fstream>
#include <ios>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace trio {

class FileStreamImpl : public FileStream {
    public:
        FileStreamImpl(const char* path_, AccessMode accessMode_, OpenMode openMode_, MemoryResource* memRes_);

        void open() override;
        void close() override;
        std::size_t tell() override;
        void seek(std::size_t position) override;
        std::size_t size() override;
        void read(char* buffer, std::size_t size) override;
        void write(const char* buffer, std::size_t size) override;

        MemoryResource* getMemoryResource();

    private:
        static sc::StatusProvider status;

        std::fstream file;
        pma::String<char> path;
        AccessMode accessMode;
        OpenMode openMode;
        std::size_t fileSize;
        MemoryResource* memRes;
};

}  // namespace trio
