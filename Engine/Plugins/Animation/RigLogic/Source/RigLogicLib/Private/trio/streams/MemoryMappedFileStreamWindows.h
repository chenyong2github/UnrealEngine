// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// *INDENT-OFF*
#ifdef TRIO_WINDOWS_FILE_MAPPING_AVAILABLE

#include "trio/streams/MemoryMappedFileStream.h"
#include "trio/types/Aliases.h"

#include <pma/TypeDefs.h>
#include <status/Provider.h>

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

#include <cstddef>
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

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

        HANDLE file;
        HANDLE mapping;
        LPVOID data;
        std::size_t position;
        pma::String<char> path;
        AccessMode accessMode;
        std::size_t fileSize;
        MemoryResource* memRes;
};

}  // namespace trio

#endif  // TRIO_WINDOWS_FILE_MAPPING_AVAILABLE
// *INDENT-ON*
