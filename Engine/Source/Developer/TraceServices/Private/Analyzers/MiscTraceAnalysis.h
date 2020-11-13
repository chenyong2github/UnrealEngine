// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Templates/SharedPointer.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Common/PagedArray.h"

namespace TraceServices
{

class IAnalysisSession;
class FThreadProvider;
class FBookmarkProvider;
class FLogProvider;
class FFrameProvider;
class FChannelProvider;

class FMiscTraceAnalyzer
	: public Trace::IAnalyzer
{
public:
	FMiscTraceAnalyzer(IAnalysisSession& Session,
					   FThreadProvider& ThreadProvider,
					   FBookmarkProvider& BookmarkProvider,
					   FLogProvider& LogProvider,
					   FFrameProvider& FrameProvider, 
					   FChannelProvider& ChannelProvider);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnThreadInfo(const FThreadInfo& ThreadInfo) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

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
		RouteId_BeginGameFrame,
		RouteId_EndGameFrame,
		RouteId_BeginRenderFrame,
		RouteId_EndRenderFrame,
		RouteId_ChannelAnnounce,
		RouteId_ChannelToggle,
	};

	struct FThreadState
	{
		TArray<const TCHAR*> ThreadGroupStack;
	};

	FThreadState* GetThreadState(uint32 ThreadId);
	void OnChannelAnnounce(const FOnEventContext& Context);
	void OnChannelToggle(const FOnEventContext& Context);

	IAnalysisSession& Session;
	FThreadProvider& ThreadProvider;
	FBookmarkProvider& BookmarkProvider;
	FLogProvider& LogProvider;
	FFrameProvider& FrameProvider;
	FChannelProvider& ChannelProvider;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStateMap;
	uint64 LastFrameCycle[TraceFrameType_Count] = { 0, 0 };
};


} // namespace TraceServices
