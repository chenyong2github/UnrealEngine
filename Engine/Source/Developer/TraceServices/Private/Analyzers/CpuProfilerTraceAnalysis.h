// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Trace.h"
#include "Trace/Analyzer.h"
#include "Containers/UnrealString.h"
#include "Templates/SharedPointer.h"
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
	FCpuProfilerAnalyzer(TSharedRef<Trace::FAnalysisSession> Session);
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
		TSharedPtr<Trace::FTimingProfilerProvider::TimelineInternal> Timeline;
	};

	TSharedRef<FThreadState> GetThreadState(uint32 ThreadId);
	uint16 DecodeSpecId(const uint8*& BufferPtr);

	enum : uint16
	{
		RouteId_EventSpec,
		RouteId_DynamicEventName,
		RouteId_EventBatch,
		RouteId_BeginGameFrame,
		RouteId_BeginRenderingFrame
	};

	TSharedRef<Trace::FAnalysisSession> Session;
	TSharedRef<Trace::FThreadProvider> ThreadProvider;
	TSharedRef<Trace::FTimingProfilerProvider> TimingProfilerProvider;
	TMap<uint32, TSharedRef<FThreadState>> ThreadStatesMap;
	TMap<uint16, uint32> ScopeIdToEventIdMap;
	TMap<uint16, uint32> DynamicNameToEventIdMap;
};
