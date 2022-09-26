// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MutableRuntime/Private/Platform.h"
#include "MemoryPrivate.h"

#include <sys/stat.h>

//#if defined(_MSC_VER) || defined(__MINGW32__)
    //#include <windef.h>
    //#include <winbase.h>
//#endif

// This file contains some hacks to solve differences between platforms necessary for the tools
// library and not present in the run-time library.


namespace mu
{

    //-------------------------------------------------------------------------------------------------
    inline FILE* mutable_fopen
		(
			const char *filename,
			const char *mode
		)
	{
		FILE* pFile = 0;

#ifdef _MSC_VER
        errno_t error = fopen_s( &pFile, filename, mode );
        if (error!=0)
        {
            // It's valid to fail, just return null.
            //check(false);
            pFile = nullptr;
        }
#else
		pFile = fopen( filename, mode );
#endif

		return pFile;
	}


    //-------------------------------------------------------------------------------------------------
    inline int64_t mutable_ftell( FILE* f )
    {
#ifdef _MSC_VER
        // Windows normal ftell works only with 32 bit int sizes
        return _ftelli64(f);
#else
        return ftell(f);
#endif
    }


    //-------------------------------------------------------------------------------------------------
    inline int64_t mutable_fseek( FILE* f, int64_t pos, int origin )
    {
#ifdef _MSC_VER
        // Windows normal fseek works only with 32 bit int sizes
        return _fseeki64(f,pos,origin);
#else
        return fseek(f,pos,origin);
#endif
    }


    //-------------------------------------------------------------------------------------------------
    inline string mutable_path_separator()
    {
    #ifdef _WIN32
        return "\\";
    #else
        return "/";
    #endif
    }


    //-------------------------------------------------------------------------------------------------
    inline bool mutable_file_exists( const string& path )
    {
        struct stat buffer;
        return (stat (path.c_str(), &buffer) == 0);
    }

}
