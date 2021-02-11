// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// *INDENT-OFF*
#ifdef TRIO_WINDOWS_FILE_MAPPING_AVAILABLE

#include "trio/streams/MemoryMappedFileStream.h"
#include "trio/streams/StreamStatus.h"
#include "trio/types/Aliases.h"

#include <pma/TypeDefs.h>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4005 4365 4987)
#endif

#ifdef TRIO_CUSTOM_WINDOWS_H
    #include TRIO_CUSTOM_WINDOWS_H
#else
    #define WIN32_LEAN_AND_MEAN
    #define NOGDICAPMASKS
    #define NOVIRTUALKEYCODES
    #define NOWINMESSAGES
    #define NOWINSTYLES
    #define NOSYSMETRICS
    #define NOMENUS
    #define NOICONS
    #define NOKEYSTATES
    #define NOSYSCOMMANDS
    #define NORASTEROPS
    #define NOSHOWWINDOW
    #define NOATOM
    #define NOCLIPBOARD
    #define NOCOLOR
    #define NOCTLMGR
    #define NODRAWTEXT
    #define NOGDI
    #define NOKERNEL
    #define NOUSER
    #define NONLS
    #define NOMB
    #define NOMEMMGR
    #define NOMETAFILE
    #define NOMINMAX
    #define NOMSG
    #define NOOPENFILE
    #define NOSCROLL
    #define NOSERVICE
    #define NOSOUND
    #define NOTEXTMETRIC
    #define NOWH
    #define NOWINOFFSETS
    #define NOCOMM
    #define NOKANJI
    #define NOHELP
    #define NOPROFILER
    #define NODEFERWINDOWPOS
    #define NOMCX
    #include <windows.h>
#endif  // TRIO_CUSTOM_WINDOWS_H
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#include <cstddef>
#include <cstdint>

namespace trio {

class MemoryMappedFileStreamWindows : public MemoryMappedFileStream {
    public:
        MemoryMappedFileStreamWindows(const char* path_, AccessMode accessMode_, MemoryResource* memRes_);
        ~MemoryMappedFileStreamWindows();

        MemoryMappedFileStreamWindows(const MemoryMappedFileStreamWindows&) = delete;
        MemoryMappedFileStreamWindows& operator=(const MemoryMappedFileStreamWindows&) = delete;

        MemoryMappedFileStreamWindows(MemoryMappedFileStreamWindows&&) = delete;
        MemoryMappedFileStreamWindows& operator=(MemoryMappedFileStreamWindows&&) = delete;

        void open() override;
        void close() override;
        std::uint64_t tell() override;
        void seek(std::uint64_t position) override;
        std::uint64_t size() override;
        std::size_t read(char* destination, std::size_t size) override;
        std::size_t read(Writable* destination, std::size_t size) override;
        std::size_t write(const char* source, std::size_t size) override;
        std::size_t write(Readable* source, std::size_t size) override;
        void flush() override;
        void resize(std::uint64_t size) override;
        const char* path() const override;
        AccessMode accessMode() const override;

        MemoryResource* getMemoryResource();

    private:
        void openFile();
        void closeFile();
        void mapFile(std::uint64_t offset, std::uint64_t size);
        void unmapFile();
        void resizeFile(std::uint64_t size);

    private:
        StreamStatus status;
        pma::String<char> filePath;
        AccessMode fileAccessMode;
        MemoryResource* memRes;
        HANDLE file;
        HANDLE mapping;
        LPVOID data;
        std::uint64_t position;
        std::uint64_t fileSize;
        std::uint64_t viewOffset;
        std::size_t viewSize;
        bool delayedMapping;
        bool dirty;
};

}  // namespace trio

#endif  // TRIO_WINDOWS_FILE_MAPPING_AVAILABLE
// *INDENT-ON*
