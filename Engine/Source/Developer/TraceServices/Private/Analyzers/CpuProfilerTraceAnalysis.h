// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Containers/UnrealString.h"
#include "Model/TimingProfiler.h"

namespace Trace
{
	class FThreadProvider;
	class FAnalysisSession;
}

class FCpuProfilerAnalyzer
	: public Trace::IAnalyzer
{
public:
	FCpuProfilerAnalyzer(Trace::FAnalysisSession& Session);
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual void OnEvent(uint16 RouteId, const FOnEventContext& Context) override;
	virtual void OnAnalysisEnd() override {};

private:
	struct EventScopeState
	{
		uint64 StartCycle;
		uint32 EventTypeId;
	};

	struct FThreadState
	{
		TArray<EventScopeState> ScopeStack;
		Trace::FTimingProfilerProvider::TimelineInternal* Timeline;
		double LastCycle = 0.0;
	};

	TSharedRef<FThreadState> GetThreadState(uint32 ThreadId);

	enum : uint16
	{
		RouteId_EventSpec,
		RouteId_EventBatch,
	};

	Trace::FAnalysisSession& Session;
	Trace::FThreadProvider& ThreadProvider;
	Trace::FTimingProfilerProvider& TimingProfilerProvider;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStatesMap;
	TMap<uint16, uint32> ScopeIdToEventIdMap;
};
