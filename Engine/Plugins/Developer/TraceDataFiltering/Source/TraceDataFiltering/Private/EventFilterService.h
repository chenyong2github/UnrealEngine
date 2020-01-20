// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "ISessionTraceFilterService.h"

namespace Trace
{
	class IAnalysisSession;
	class ISessionService;
	typedef uint64 FSessionHandle;
}

class FEventFilterService
{
protected:
	FEventFilterService();
public:
	/** Singleton-getter */
	static FEventFilterService& Get();

	/** Retrieves the ISessionTraceFilterService for the provided Trace Session Handle */
	TSharedRef<ISessionTraceFilterService> GetFilterServiceByHandle(Trace::FSessionHandle InHandle);

protected:
	/** Currently active set of Trace Analysis sessions */
	TArray<TSharedPtr<const Trace::IAnalysisSession>> AnalysisSessions;
	
	/** TraceFilterServices for corresponding Trace Analysis sessions */
	TMap<Trace::FSessionHandle, TSharedPtr<ISessionTraceFilterService>> PerHandleFilterService;

	/** Cached instance of the Trace Session Service */
	TSharedPtr<Trace::ISessionService> SessionService;
};