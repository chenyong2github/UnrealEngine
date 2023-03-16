// Copyright Epic Games, Inc. All Rights Reserved.

#if (defined(__AUTORTFM) && __AUTORTFM)
#include "Utils.h"
#include <errno.h>
#include <mutex>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#else
#include <execinfo.h>
#endif

namespace AutoRTFM
{

// To log to a file, comment out LogFilename = nullptr and uncomment the other one.
constexpr const char* LogFilename = nullptr;
// constexpr const char* LogFilename = "log.txt";

constexpr const char* LogFileMode = "wt";

std::once_flag LogInitializationFlag;
FILE* LogFile;

FILE* GetLogFile()
{
    std::call_once(LogInitializationFlag, []
    {
        if (LogFilename)
        {
            ASSERT(LogFileMode);

#ifdef _MSC_VER
/*
   Disable warning about deprecated STD C functions.
*/
#pragma warning(disable : 4996)

#pragma warning(push)
#endif

            LogFile = fopen(LogFilename, LogFileMode);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

            if (!LogFile)
            {
                fprintf(stderr, "Could not open %s: %s\n", LogFilename, strerror(errno));
            }
        }
        if (!LogFile)
        {
            LogFile = stderr;
        }
        setvbuf(LogFile, NULL, _IONBF, 0);
    });
    return LogFile;
}

std::string GetFunctionDescription(void* FunctionPtr)
{
#ifdef _WIN32
    // This is gross, but it works. It's possible for someone to have SymInitialized before. But if they had, then this
    // will just fail. Also, this function is called in cases where we're failing, so it's ok if we do dirty things.
    SymInitialize(GetCurrentProcess(), nullptr, true);
    
    DWORD64 Displacement = 0;
    DWORD64 Address = reinterpret_cast<DWORD64>(FunctionPtr);
    char Buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO Symbol = reinterpret_cast<PSYMBOL_INFO>(Buffer);
    Symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    Symbol->MaxNameLen = MAX_SYM_NAME;
    if (SymFromAddr(GetCurrentProcess(), Address, &Displacement, Symbol))
    {
        return Symbol->Name;
    }
    else
    {
        return "<error getting description>";
    }
#else // _WIN32 -> so !_WIN32
    char** const symbols = backtrace_symbols(&FunctionPtr, 1);
    std::string name(*symbols);
    free(symbols);
    return name;
#endif // _WIN32 -> so end of !_WIN32
}

} // namespace AutoRTFM

#endif // defined(__AUTORTFM) && __AUTORTFM