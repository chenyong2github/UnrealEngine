// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


/*=============================================================================================
	HTML5String.h: HTML5 platform string classes, mostly implemented with ANSI C++
==============================================================================================*/

#pragma once
#include "GenericPlatform/StandardPlatformString.h"
#include "CoreTypes.h"

struct FHTML5PlatformString : public FStandardPlatformString
{
	static const ANSICHAR* GetEncodingName()
	{
		return "UTF-32LE"; // or should this be UTF-8?
	}
};

// default implementation
typedef FHTML5PlatformString FPlatformString;

// Format specifiers to be able to print values of these types correctly, for example when using UE_LOG.
// SIZE_T format specifier
#define SIZE_T_FMT "lu"
// SIZE_T format specifier for lowercase hexadecimal output
#define SIZE_T_x_FMT "lx"
// SIZE_T format specifier for uppercase hexadecimal output
#define SIZE_T_X_FMT "lX"

// SSIZE_T format specifier
#define SSIZE_T_FMT "ld"
// SSIZE_T format specifier for lowercase hexadecimal output
#define SSIZE_T_x_FMT "lx"
// SSIZE_T format specifier for uppercase hexadecimal output
#define SSIZE_T_X_FMT "lX"

// PTRINT format specifier for decimal output
#define PTRINT_FMT "d"
// PTRINT format specifier for lowercase hexadecimal output
#define PTRINT_x_FMT "x"
// PTRINT format specifier for uppercase hexadecimal output
#define PTRINT_X_FMT "X"

// UPTRINT format specifier for decimal output
#define UPTRINT_FMT "u"
// UPTRINT format specifier for lowercase hexadecimal output
#define UPTRINT_x_FMT "x"
// UPTRINT format specifier for uppercase hexadecimal output
#define UPTRINT_X_FMT "X"

// int64 format specifier for decimal output
#define INT64_FMT "lld"
// int64 format specifier for lowercase hexadecimal output
#define INT64_x_FMT "llx"
// int64 format specifier for uppercase hexadecimal output
#define INT64_X_FMT "llX"

// uint64 format specifier for decimal output
#define UINT64_FMT "llu"
// uint64 format specifier for lowercase hexadecimal output
#define UINT64_x_FMT "llx"
// uint64 format specifier for uppercase hexadecimal output
#define UINT64_X_FMT "llX"
