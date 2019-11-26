// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Analyzer.h"
#include "Containers/UnrealString.h"
#include "Model/TimingProfilerPrivate.h"

namespace Trace
{
	class IAnalysisSession;
}

class FCpuProfilerAnalyzer
	: public Trace::IAnalyzer
{
public:
	FCpuProfilerAnalyzer(Trace::IAnalysisSession& Session, Trace::FTimingProfilerProvider& TimingProfilerProvider);
	~FCpuProfilerAnalyzer();
	virtual void OnAnalysisBegin(const FOnAnalysisContext& Context) override;
	virtual bool OnEvent(uint16 RouteId, const FOnEventContext& Context) override;

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

	void DefineScope(uint32 Id, const TCHAR* ScopeName);
	FThreadState& GetThreadState(uint32 ThreadId);

	enum : uint16
	{
		RouteId_EventSpec,
		RouteId_EventBatch,
		RouteId_EndCapture,
	};

	Trace::IAnalysisSession& Session;
	Trace::FTimingProfilerProvider& TimingProfilerProvider;
	TMap<uint32, FThreadState*> ThreadStatesMap;
	TMap<uint32, uint32> ScopeIdToEventIdMap;
	TMap<const TCHAR*, uint32> ScopeNameToEventIdMap;
	uint64 TotalEventSize = 0;
	uint64 TotalScopeCount = 0;
	double BytesPerScope = 0.0;
};
