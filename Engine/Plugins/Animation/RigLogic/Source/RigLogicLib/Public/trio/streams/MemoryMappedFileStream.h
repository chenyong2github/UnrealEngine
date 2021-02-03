// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "trio/Defs.h"
#include "trio/Stream.h"
#include "trio/types/Aliases.h"
#include "trio/types/Parameters.h"

namespace trio {

/**
    @brief Memory mapped file stream.
*/
class TRIOAPI MemoryMappedFileStream : public BoundedIOStream {
    public:
        using AccessMode = trio::AccessMode;

        static const sc::StatusCode OpenError;
        static const sc::StatusCode ReadError;
        static const sc::StatusCode WriteError;
        static const sc::StatusCode AlreadyOpenError;

    public:
        /**
            @brief Factory method for creation of a MemoryMappedFileStream instance.
            @param path
                Path to file to be opened.
            @param accessMode
                Control whether the file is opened for reading or writing.
            @param memRes
                The memory resource to be used for the allocation of the MemoryMappedFileStream instance.
            @note
                If a custom memory resource is not given, a default allocation mechanism will be used.
            @warning
                User is responsible for releasing the returned pointer by calling destroy.
            @see destroy
        */
        static MemoryMappedFileStream* create(const char* path, AccessMode accessMode, MemoryResource* memRes = nullptr);
        /**
            @brief Method for freeing a MemoryMappedFileStream instance.
            @param instance
                Instance of MemoryMappedFileStream to be freed.
            @see create
        */
        static void destroy(MemoryMappedFileStream* instance);

        MemoryMappedFileStream() = default;
        ~MemoryMappedFileStream() override;

        MemoryMappedFileStream(const MemoryMappedFileStream&) = delete;
        MemoryMappedFileStream& operator=(const MemoryMappedFileStream&) = delete;

        MemoryMappedFileStream(MemoryMappedFileStream&&) = default;
        MemoryMappedFileStream& operator=(MemoryMappedFileStream&&) = default;

        /**
            @brief Flush the changed contents of the mapped file to disk.
        */
        virtual void flush() = 0;
        /**
            @brief Resize file to the requested size.
            @note Exposed to avoid lots of remapping if a file is created from scratch.
        */
        virtual void resize(std::size_t size) = 0;

};

}  // namespace trio

namespace pma {

template<>
struct DefaultInstanceCreator<trio::MemoryMappedFileStream> {
    using type = FactoryCreate<trio::MemoryMappedFileStream>;
};

template<>
struct DefaultInstanceDestroyer<trio::MemoryMappedFileStream> {
    using type = FactoryDestroy<trio::MemoryMappedFileStream>;
};

}  // namespace pma
