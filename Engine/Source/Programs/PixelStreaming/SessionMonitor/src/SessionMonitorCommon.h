// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#define EG_PLATFORM_WINDOWS 1
#define EG_PLATFORM_LINUX 2

#if defined(_WIN32)
	#define EG_PLATFORM EG_PLATFORM_WINDOWS
#elif __linux__
	#define EG_PLATFORM EG_PLATFORM_LINUX
#endif

// Set any configuration flags not defined.
// This allows just specifying e.g EG_BUILD_DEBUG=1 in the project, and have the other
// ones automatically set to 0
#ifndef EG_BUILD_DEBUG
	#define EG_BUILD_DEBUG 0
#endif
#ifndef EG_BUILD_DEVELOPMENT
	#define EG_BUILD_DEVELOPMENT 0
#endif
#ifndef EG_BUILD_SHIPPING
	#define EG_BUILD_SHIPPING 0
#endif
#ifndef USE_CHECK_IN_SHIPPING
	#define USE_CHECK_IN_SHIPPING 0
#endif
#ifndef DO_GUARD_SLOW
	#define DO_GUARD_SLOW 0
#endif

/**
 * Forceful assert, even on Release builds
 */ 
void DoAssert(const char* File, int Line, _Printf_format_string_ const char* Fmt, ...);

//////////////////////////////////////////////////////////////////////////
// check and verify macros work in a similar way to Unreal Engine
//
// "check" expressions are runtime asserts that are compiled out in Shipping builds,
// unless USE_CHECK_IN_SHIPPING is 1
//
// "verify" expressions are ALWAYS evaluated, but they don't halt execution in Shipping builds
// unless USE_CHECK_IN_SHIPPING is 1
//
// "checkSlow/checkfSlow" macros do the same as the normal check/checkf, but
// are compiled out in Development and Shipping. It's meant to be used for checks that are
// quite pedantic and might affect performance in Development.
// If you want these to be be enabled in Development and even Shipping (provided USE_CHECK_IN_SHIPPING is 1),
// then set DO_GUARD_SLOW to 1
//
//////////////////////////////////////////////////////////////////////////


//
// Check macros
//
#if EG_BUILD_DEBUG || EG_BUILD_DEVELOPMENT || (EG_BUILD_SHIPPING && USE_CHECK_IN_SHIPPING)
	#define check(Exp) if (!(Exp)) { ::DoAssert(__FILE__, __LINE__, #Exp); }
	#define checkf(Exp, Fmt, ...) if (!(Exp)) { ::DoAssert(__FILE__, __LINE__, Fmt, ##__VA_ARGS__); } // By using ##__VA_ARGS__ , it will remove the last comma, if __VA_ARGS__ is empty
#else
	#define check(Exp) ((void)0)
	#define checkf(Exp, Fmt, ...) ((void)0)
#endif

//
// Check slow macros
#if EG_BUILD_DEBUG || (EG_BUILD_DEVELOPMENT && DO_GUARD_SLOW) || (EG_BUILD_SHIPPING && USE_CHECK_IN_SHIPPING && DO_GUARD_SLOW)
	#define checkSlow(Exp) if (!(Exp)) { ::DoAssert(__FILE__, __LINE__, #Exp); }
	#define checkfSlow(Exp, Fmt, ...) if (!(Exp)) { ::DoAssert(__FILE__, __LINE__, Fmt, ##__VA_ARGS__); } // By using ##__VA_ARGS__ , it will remove the last comma, if __VA_ARGS__ is empty
#else
	#define checkSlow(Exp) ((void)0)
	#define checkfSlow(Exp, Fmt, ...) ((void)0)
#endif


//
// verify macros
//
#if EG_BUILD_DEBUG || EG_BUILD_DEVELOPMENT || (EG_BUILD_SHIPPING && USE_CHECK_IN_SHIPPING)
	#define verify(Exp) if (!(Exp)) { ::DoAssert(__FILE__, __LINE__, #Exp); }
	#define verifyf(Exp, Fmt, ...) if (!(Exp)) { ::DoAssert(__FILE__, __LINE__, Fmt, ##__VA_ARGS__); } // By using ##__VA_ARGS__ , it will remove the last comma, if __VA_ARGS__ is empty
#else
	#define verify(Exp) if (!(Exp)) {}
	#define verifyf(Exp, Fmt, ...) if (!(Exp)) {}
#endif

//
// Available parameters
//
extern std::pair<std::string, uint16_t> PARAM_Cirrus;
extern uint16_t PARAM_UE4Port;
extern bool PARAM_PlanB;
extern bool PARAM_LocalTime;
extern std::string RootDir;

#if EG_PLATFORM == EG_PLATFORM_WINDOWS
	#pragma warning(disable: 26439) // This kind of function may not throw. Declare it 'noexcept' (f.6).
	#pragma warning(disable: 26444) // warning C26444: Avoid unnamed objects with custom construction and destruction (es.84).
	#pragma warning(disable: 6319) // Use of the comma-operator in a tested expression causes the left argument to be ignored when it has no side-effects.
#endif

extern DWORD MainThreadId;
#define CHECK_MAINTHREAD() \
	verify(MainThreadId == ::GetCurrentThreadId())

//! Generic Log macro, that prefixes the child app name.
// It needs a "GetAppName()" function to be reachable.
#define APPLOG(Severity, Fmt, ...) \
	EG_LOG(LogDefault, Severity, (GetAppName() + ": " + Fmt).c_str(), ##__VA_ARGS__)
