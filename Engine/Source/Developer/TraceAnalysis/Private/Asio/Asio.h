// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(TRACE_WITH_ASIO)
#	define TRACE_WITH_ASIO 0
#endif // 0

#if TRACE_WITH_ASIO

#if PLATFORM_WINDOWS
#	include "Windows/AllowWindowsPlatformTypes.h"
#	include "Windows/AllowWindowsPlatformAtomics.h"
#endif

THIRD_PARTY_INCLUDES_START

namespace asio {
namespace detail {

template <typename ExceptionType>
void throw_exception(const ExceptionType& Exception)
{
	/* Intentionally blank */
}

} // namespace detail
} // namespace asio

#include "asio/connect.hpp"
#include "asio/io_context.hpp"
#include "asio/ip/tcp.hpp"
#include "asio/read.hpp"
#include "asio/write.hpp"
#include "asio/write_at.hpp"

#if PLATFORM_WINDOWS
#	include "asio/windows/random_access_handle.hpp"
#endif

THIRD_PARTY_INCLUDES_END

#if PLATFORM_WINDOWS
#	include "Windows/HideWindowsPlatformAtomics.h"
#	include "Windows/HideWindowsPlatformTypes.h"
#endif

#endif // TRACE_WITH_ASIO
