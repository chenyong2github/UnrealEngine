// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Common/PagedArray.h"

namespace Trace
{
	class FAnalysisSession;
	class FThreadProvider;
	class FBookmarkProvider;
	class FLogProvider;
	class FFrameProvider;
}

class FMiscTraceAnalyzer
	: public Trace::IAnalyzer
{
public:
	FMiscTraceAnalyzer(TSharedRef<Trace::FAnalysisSession> Session);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override {};

private:
	enum : uint16
	{
		RouteId_RegisterGameThread,
		RouteId_CreateThread,
		RouteId_SetThreadGroup,
		RouteId_BeginThreadGroupScope,
		RouteId_EndThreadGroupScope,
		RouteId_BookmarkSpec,
		RouteId_Bookmark,
		RouteId_BeginFrame,
		RouteId_EndFrame,
	};

	struct FThreadState
	{
		TArray<ETraceThreadGroup> ThreadGroupStack;
	};

	FThreadState* GetThreadState(uint32 ThreadId);

	TSharedRef<Trace::FAnalysisSession> Session;
	TSharedRef<Trace::FThreadProvider> ThreadProvider;
	TSharedRef<Trace::FBookmarkProvider> BookmarkProvider;
	TSharedRef<Trace::FLogProvider> LogProvider;
	TSharedRef<Trace::FFrameProvider> FrameProvider;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStateMap;
};

