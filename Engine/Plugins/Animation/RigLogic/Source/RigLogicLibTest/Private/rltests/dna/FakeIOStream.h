// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <trio/Stream.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4365 4987)
#endif
#include <cassert>
#include <cstring>
#include <cstddef>
#include <vector>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

namespace rltests {

struct FakeIOStream : public trio::BoundedIOStream {

    FakeIOStream() : position{} {
    }

    ~FakeIOStream();

    void open() override {
        position = 0ul;
    }

    void close() override {
        position = 0ul;
    }

    std::size_t tell() override {
        return position;
    }

    void seek(std::size_t position_) override {
        position = position_;
    }

    void read(char* buffer, std::size_t size) override {
        assert(position + size <= data.size());
        std::memcpy(buffer, &data[position], size);
        position += size;
    }

    void write(const char* buffer, std::size_t size) override {
        std::size_t available = data.size() - position;
        if (available < size) {
            data.resize(data.size() + (size - available));
        }
        std::memcpy(&data[position], buffer, size);
        position += size;
    }

    std::size_t size() override {
        return data.size();
    }

    std::vector<char> data;
    std::size_t position;
};

}  // namespace rltests
