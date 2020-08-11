// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Containers/UnrealString.h"
#include "Model/TimingProfilerPrivate.h"

namespace Trace
{
	class IAnalysisSession;
	class FThreadProvider;
}

class FCpuProfilerAnalyzer
	: public Trace::IAnalyzer
{
public:
	FCpuProfilerAnalyzer(Trace::IAnalysisSession& Session, Trace::FTimingProfilerProvider& TimingProfilerProvider, Trace::FThreadProvider& InThreadProvider);
	~FCpuProfilerAnalyzer();
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context) override;

private:
	struct FEventScopeState
	{
		uint64 StartCycle;
		uint32 EventTypeId;
	};

	struct FPendingEvent
	{
		uint64 Cycle;
		uint32 TimerId;
	};

	struct FThreadState
	{
		TArray<FEventScopeState> ScopeStack;
		TArray<FPendingEvent> PendingEvents;
		Trace::FTimingProfilerProvider::TimelineInternal* Timeline;
		uint64 LastCycle = 0;
	};

	void OnCpuScopeEnter(const FOnEventContext& Context);
	void OnCpuScopeLeave(const FOnEventContext& Context);
	void DefineScope(uint32 SpecId, const TCHAR* ScopeName);
	FThreadState& GetThreadState(uint32 ThreadId);
	uint64 ProcessBuffer(const FEventTime& EventTime, uint32 ThreadId, const uint8* BufferPtr, uint32 BufferSize);

	enum : uint16
	{
		RouteId_EventSpec,
		RouteId_EventBatch,
		RouteId_EndThread,
		RouteId_EndCapture,
		RouteId_CpuScope,
		RouteId_ChannelAnnounce,
		RouteId_ChannelToggle,
	};

	Trace::IAnalysisSession& Session;
	Trace::FTimingProfilerProvider& TimingProfilerProvider;
	Trace::FThreadProvider& ThreadProvider;
	TMap<uint32, FThreadState*> ThreadStatesMap;
	TMap<uint32, uint32> SpecIdToTimerIdMap;
	TMap<const TCHAR*, uint32> ScopeNameToTimerIdMap;
	uint64 TotalEventSize = 0;
	uint64 TotalScopeCount = 0;
	double BytesPerScope = 0.0;
};
