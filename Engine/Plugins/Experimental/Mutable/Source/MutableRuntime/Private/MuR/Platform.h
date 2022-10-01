// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// This file contains some hacks to solve differences between platforms
// This file also contains wrappers for all the libc methods used in the runtime.
// No other place in the runtime should include any libc header.

#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"
#include "Stats/Stats.h"

//! There is a define MUTABLE_PROFILE that can be passed on the compiler options to enable
//! the internal profiling methods. See Config.h for more information.

#include "MuR/Types.h"
#include "HAL/UnrealMemory.h"
#include <cstdint>

MUTABLERUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogMutableCore, Log, All);


#ifdef __ANDROID__
    #define MUTABLE_PLATFORM_ANDROID
#elif defined(_XBOX_ONE)
	#define MUTABLE_PLATFORM_XBOXONE
#elif defined(__PS4__)
	#define MUTABLE_PLATFORM_PS4
#elif defined(__PS5__)
	#define MUTABLE_PLATFORM_PS5
#elif defined(__SWITCH__)
    #define MUTABLE_PLATFORM_SWITCH
#elif defined(_WIN64) && _WIN64
    #define MUTABLE_PLATFORM_WIN64
#elif defined(_WIN32) && _WIN32
    #define MUTABLE_PLATFORM_WIN32
#elif defined(__APPLE__) && __APPLE__
    #include "TargetConditionals.h"
    #if TARGET_IPHONE_SIMULATOR
        #define MUTABLE_PLATFORM_IOS
    #elif TARGET_OS_IPHONE
        #define MUTABLE_PLATFORM_IOS
    #elif TARGET_OS_MAC
        #define MUTABLE_PLATFORM_OSX
    #else
        // Unsupported platform
        #warning "Unidentified platform."
    #endif
#elif defined(__linux) && __linux
    #define MUTABLE_PLATFORM_LINUX
#elif defined(__unix) && __unix // all unices not caught above
    #define MUTABLE_PLATFORM_LINUX
#elif defined(__posix) && __posix
    #define MUTABLE_PLATFORM_LINUX
#endif


#if defined(MUTABLE_PLATFORM_XBOXONE)

	#define NOMINMAX
	#include <xdk.h>
	#include <wrl.h>

#elif defined(MUTABLE_PLATFORM_WIN64) || defined(MUTABLE_PLATFORM_WIN32)

	#ifdef _MSC_VER

		#ifndef _HAS_EXCEPTIONS
			#define _HAS_EXCEPTIONS 0
		#endif

		#ifndef WIN32_LEAN_AND_MEAN
			#define WIN32_LEAN_AND_MEAN
		#endif

        #define NOMINMAX
        //#include <windows.h>

		// Some warnings that are more annoying than useful
		#pragma warning( disable : 4127 )
		#pragma warning( disable : 6385 )
		#pragma warning( disable : 6287 )


    #else

        // Some unix environment under windows.
        #include <sys/time.h>

	#endif

#elif defined(MUTABLE_PLATFORM_ANDROID)

    #include <sys/time.h>

#else

    #include <sys/time.h>

#endif


//! Unify debug defines
#if !defined(MUTABLE_DEBUG)
    #if !defined(NDEBUG) || defined(_DEBUG)
        #define MUTABLE_DEBUG
    #endif
#endif


namespace mu
{

    inline void* mutable_system_malloc( size_t size, uint32_t alignment )
    {        
        return FMemory::Malloc(size,alignment);
    }


    inline void mutable_system_free( void* ptr )
    {
		FMemory::Free(ptr);
    }


	//---------------------------------------------------------------------------------------------
	inline int mutable_memcmp ( const void * a, const void* b, size_t num )
	{
		return memcmp( a, b, num );
	}


	//---------------------------------------------------------------------------------------------
	inline int mutable_vsnprintf
		( char *buffer, size_t sizeOfBuffer, const char *format, va_list argptr )
	{
		#ifdef _MSC_VER
			return vsnprintf_s( buffer, sizeOfBuffer, _TRUNCATE, format, argptr );
		#else
			return vsnprintf( buffer, sizeOfBuffer, format, argptr );
		#endif
	}


	//---------------------------------------------------------------------------------------------
	inline int mutable_snprintf
		( char *buffer, size_t sizeOfBuffer, const char *format, ... )
	{
		va_list ap;
		va_start( ap, format );
		int res = mutable_vsnprintf( buffer, sizeOfBuffer, format, ap );
		va_end(ap);
		return res;
	}


	//---------------------------------------------------------------------------------------------
	// Disgracefully halt the program.
	inline void Halt()
	{
		#ifdef _MSC_VER
            __debugbreak();
		#else
			__builtin_trap();
		#endif
	}


	//---------------------------------------------------------------------------------------------
	//! Simple profiling timer.
	//! It should only be used in debug and profile builds.
	//---------------------------------------------------------------------------------------------
	struct TIMER
	{
	public:

		//!
		inline void Start()
		{
#ifdef MUTABLE_PROFILE
			m_start = int64( FPlatformTime::Seconds()*1000000.0 );
#endif
		}

		//! Stop counting and return the ellapsed time since Start in milliseconds.
		inline int GetMilliseconds() const
		{
#ifdef MUTABLE_PROFILE
			return int( (int64(FPlatformTime::Seconds() * 1000000.0) - m_start)/1000 );
#else
			return 0;
#endif
		}

		//! Stop counting and return the ellapsed time since Start in microseconds.
		inline int GetMicroseconds() const
		{
#ifdef MUTABLE_PROFILE
			return int((int64(FPlatformTime::Seconds() * 1000000.0) - m_start));
#else
			return 0;
#endif
		}

	private:

#ifdef MUTABLE_PROFILE
	int64_t m_start=0;
#endif
	};

}
