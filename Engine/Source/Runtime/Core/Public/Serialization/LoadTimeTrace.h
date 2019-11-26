// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Config.h"
#include "ProfilingDebugging/FormatArgsTrace.h"

#if !defined(LOADTIMEPROFILERTRACE_ENABLED)
#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define LOADTIMEPROFILERTRACE_ENABLED 1
#else
#define LOADTIMEPROFILERTRACE_ENABLED 0
#endif
#endif

#if LOADTIMEPROFILERTRACE_ENABLED

struct FLoadTimeProfilerTrace
{
	struct FRequestGroupScope
	{
		template <typename... Types>
		FRequestGroupScope(const TCHAR* InFormatString, Types... FormatArgs)
		{
			FormatString = InFormatString;
			FormatArgsSize = FFormatArgsTrace::EncodeArguments(FormatArgsBuffer, FormatArgs...);
			OutputBegin();
		}

		CORE_API ~FRequestGroupScope();

	private:
		CORE_API void OutputBegin();

		const TCHAR* FormatString = nullptr;
		uint16 FormatArgsSize = 0;
		uint8 FormatArgsBuffer[1024];
	};

	CORE_API static void InitInternal();
};

#define TRACE_LOADTIME_REQUEST_GROUP_SCOPE(Format, ...) \
	FLoadTimeProfilerTrace::FRequestGroupScope __LoadTimeTraceRequestGroupScope(Format, ##__VA_ARGS__);

#else
#define TRACE_LOADTIME_REQUEST_GROUP_SCOPE(...)
#endif
