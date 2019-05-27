// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CpuProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "TraceServices/Model/Threads.h"

FCpuProfilerAnalyzer::FCpuProfilerAnalyzer(Trace::IAnalysisSession& InSession, Trace::FTimingProfilerProvider& InTimingProfilerProvider)
	: Session(InSession)
	, TimingProfilerProvider(InTimingProfilerProvider)
{

}

void FCpuProfilerAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_EventSpec, "CpuProfiler", "EventSpec");
	Builder.RouteEvent(RouteId_EventBatch, "CpuProfiler", "EventBatch");
}

void FCpuProfilerAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint16 Id = EventData.GetValue("Id").As<uint16>();
		ScopeIdToEventIdMap.Add(Id, TimingProfilerProvider.AddCpuTimer(reinterpret_cast<const TCHAR*>(EventData.GetAttachment())));
		break;
	}
	case RouteId_EventBatch:
	{
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		uint64 LastCycle = ThreadState->LastCycle;
		uint64 BufferSize = EventData.GetAttachmentSize();
		const uint8* BufferPtr = EventData.GetAttachment();
		const uint8* BufferEnd = BufferPtr + BufferSize;
		while (BufferPtr < BufferEnd)
		{
			uint64 DecodedCycle = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 ActualCycle = (DecodedCycle >> 1) + LastCycle;
			LastCycle = ActualCycle;
			if (DecodedCycle & 1ull)
			{
				EventScopeState& ScopeState = ThreadState->ScopeStack.AddDefaulted_GetRef();
				ScopeState.StartCycle = ActualCycle;
				uint16 SpecId = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
				ScopeState.EventTypeId = ScopeIdToEventIdMap[SpecId];
				Trace::FTimingProfilerEvent Event;
				Event.TimerIndex = ScopeState.EventTypeId;
				ThreadState->Timeline->AppendBeginEvent(Context.SessionContext.TimestampFromCycle(ScopeState.StartCycle), Event);
			}
			else
			{
				check(ThreadState->ScopeStack.Num());
				ThreadState->ScopeStack.Pop();
				ThreadState->Timeline->AppendEndEvent(Context.SessionContext.TimestampFromCycle(ActualCycle));
			}
		}
		check(BufferPtr == BufferEnd);
		if (LastCycle)
		{
			Session.UpdateDurationSeconds(Context.SessionContext.TimestampFromCycle(LastCycle));
		}
		ThreadState->LastCycle = LastCycle;
		break;
	}
	}
}

TSharedRef<FCpuProfilerAnalyzer::FThreadState> FCpuProfilerAnalyzer::GetThreadState(uint32 ThreadId)
{
	if (!ThreadStatesMap.Contains(ThreadId))
	{
		TSharedRef<FThreadState> ThreadState = MakeShared<FThreadState>();
		ThreadState->Timeline = &TimingProfilerProvider.EditCpuThreadTimeline(ThreadId);
		ThreadStatesMap.Add(ThreadId, ThreadState);
		return ThreadState;
	}
	else
	{
		return ThreadStatesMap[ThreadId];
	}
}

