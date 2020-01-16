// Copyright Epic Games, Inc. All Rights Reserved.

#include "Asio.h"

#if TRACE_WITH_ASIO

#include "HAL/Platform.h"
#include "Misc/AssertionMacros.h"

#if PLATFORM_WINDOWS
#	include "Windows/AllowWindowsPlatformTypes.h"
#	include "Windows/AllowWindowsPlatformAtomics.h"
#endif

#if defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable : 4191)
#endif

THIRD_PARTY_INCLUDES_START

#include "asio/impl/src.hpp"

THIRD_PARTY_INCLUDES_END

#if defined(_MSC_VER)
#	pragma warning(pop)
#endif

#if PLATFORM_WINDOWS
#	include "Windows/HideWindowsPlatformAtomics.h"
#	include "Windows/HideWindowsPlatformTypes.h"
#endif

#endif // TRACE_WITH_ASIO
