// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "trio/Defs.h"

#include <cstddef>

namespace trio {

class TRIOAPI Readable {
    public:
        /**
            @brief Read bytes from stream into the given buffer.
            @param buffer
                Destination buffer into which the data is going to be read from the stream.
            @param size
                Number of bytes to read from stream.
        */
        virtual void read(char* buffer, std::size_t size) = 0;

    protected:
        virtual ~Readable();

};

class TRIOAPI Writable {
    public:
        /**
            @brief Writes bytes from the given buffer to the stream.
            @param buffer
                Source buffer from which the data is going to be written to the stream.
            @param size
                Number of bytes to write to the stream.
        */
        virtual void write(const char* buffer, std::size_t size) = 0;

    protected:
        virtual ~Writable();

};

class TRIOAPI Seekable {
    public:
        /**
            @brief Get the current position in the stream.
            @return
                Position in the stream relative to it's start, with 0 denoting the start position.
        */
        virtual std::size_t tell() = 0;
        /**
            @brief Set the current position in the stream.
            @param position
                Position in the stream relative to it's start, with 0 denoting the start position.
        */
        virtual void seek(std::size_t position) = 0;

    protected:
        virtual ~Seekable();

};

class TRIOAPI Openable {
    public:
        /**
            @brief Open access to the stream.
        */
        virtual void open() = 0;

    protected:
        virtual ~Openable();

};

class TRIOAPI Closeable {
    public:
        /**
            @brief Close access to the stream.
        */
        virtual void close() = 0;

    protected:
        virtual ~Closeable();

};

class TRIOAPI Controllable : public Openable, public Closeable {
    protected:
        virtual ~Controllable();

};

}  // namespace trio
