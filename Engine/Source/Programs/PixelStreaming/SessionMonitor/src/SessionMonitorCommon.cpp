// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorPCH.h"
#include "SessionMonitorCommon.h"
#include "StringUtils.h"
#include "Logging.h"

namespace detail
{
	void BreakImpl()
	{
#if EG_PLATFORM == EG_PLATFORM_WINDOWS
		__debugbreak();
#elif EG_PLATFORM == EG_PLATFORM_LINUX
		#error Not implemented yet
#else
		#error Unknown platform
#endif
	}
}

void DoAssert(const char* File, int Line, _Printf_format_string_ const char* Fmt, ...)
{
	// The actual call to break
	auto DoBreak = []() {
		detail::BreakImpl();
		exit(EXIT_FAILURE);
	};

	// Detect reentrancy, since we call a couple of things from here that
	// can end up asserting
	static bool Executing;
	if (Executing)
	{
		DoBreak();
		return;
	}
	Executing = true;

	char Msg[1024];
	va_list Args;
	va_start(Args, Fmt);
	VSNPrintf(Msg, 1024, Fmt, Args);
	va_end(Args);

	EG_LOG(LogDefault, Error, "ASSERT: %s, %d: %s\n", File, Line, Msg);

	DoBreak();
}

