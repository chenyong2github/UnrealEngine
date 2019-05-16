// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

////////////////////////////////////////////////////////////////////////////////
#if !IS_PROGRAM && !defined(UE_TRACE_ENABLED)
#	if PLATFORM_WINDOWS || PLATFORM_PS4 || PLATFORM_XBOXONE
#		define UE_TRACE_ENABLED	1
#	else
#	endif
#endif // !IS_PROGRAM

#if !defined(UE_TRACE_ENABLED)
#	define UE_TRACE_ENABLED		0
#endif

#if UE_TRACE_ENABLED
#	define UE_TRACE_IMPL(...)
#	define UE_TRACE_API			TRACELOG_API
#else
#	define UE_TRACE_IMPL(...)	{ return __VA_ARGS__; }
#	define UE_TRACE_API			inline
#endif
