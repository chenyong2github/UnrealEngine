// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "trio/streams/MemoryStream.h"
#include "trio/types/Aliases.h"

#include <pma/TypeDefs.h>
#include <status/Provider.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cstddef>
#include <vector>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace trio {

class MemoryStreamImpl : public MemoryStream {
    public:
        MemoryStreamImpl(std::size_t initialSize, MemoryResource* memRes_);

        void open() override;
        void close() override;
        std::size_t tell() override;
        void seek(std::size_t position_) override;
        std::size_t size() override;
        void read(char* buffer, std::size_t size) override;
        void write(const char* buffer, std::size_t size) override;

        MemoryResource* getMemoryResource();

    private:
        static sc::StatusProvider status;

        Vector<char> data;
        std::size_t position;
        MemoryResource* memRes;
};

}  // namespace trio
