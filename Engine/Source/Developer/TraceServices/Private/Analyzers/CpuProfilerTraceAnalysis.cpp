// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CpuProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "Model/Threads.h"

FCpuProfilerAnalyzer::FCpuProfilerAnalyzer(TSharedRef<Trace::FAnalysisSession> InSession)
	: Session(InSession)
	, ThreadProvider(InSession->EditThreadProvider())
	, TimingProfilerProvider(InSession->EditTimingProfilerProvider())
{

}

void FCpuProfilerAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_EventSpec, "CpuProfiler", "EventSpec");
	Builder.RouteEvent(RouteId_DynamicEventName, "CpuProfiler", "DynamicEventName");
	Builder.RouteEvent(RouteId_EventBatch, "CpuProfiler", "EventBatch");
}

void FCpuProfilerAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session.Get());

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint16 Id = EventData.GetValue("Id").As<uint16>();
		ScopeIdToEventIdMap.Add(Id, TimingProfilerProvider->AddCpuTimer(*FString((const char*)EventData.GetAttachment(), EventData.GetValue("NameSize").As<uint16>())));
		break;
	}
	case RouteId_DynamicEventName:
	{
		uint16 Id = EventData.GetValue("Id").As<uint16>();
		if (DynamicNameToEventIdMap.Contains(Id))
		{
			DynamicNameToEventIdMap[Id] = TimingProfilerProvider->AddCpuTimer(*FString((const char*)EventData.GetAttachment(), EventData.GetValue("NameSize").As<uint16>()));
		}
		else
		{
			DynamicNameToEventIdMap.Add(Id, TimingProfilerProvider->AddCpuTimer(*FString((const char*)EventData.GetAttachment(), EventData.GetValue("NameSize").As<uint16>())));
		}
		break;
	}
	case RouteId_EventBatch:
	{
		uint32 ThreadId = EventData.GetValue("ThreadId").As<uint32>();
		TSharedRef<FThreadState> ThreadState = GetThreadState(ThreadId);
		uint64 LastCycle = EventData.GetValue("CycleBase").As<uint64>();
		uint64 BufferSize = EventData.GetValue("BufferSize").As<uint8>();
		const uint8* BufferPtr = EventData.GetAttachment();
		const uint8* BufferEnd = BufferPtr + BufferSize;
		uint64 LatestEndCycle = 0;
		while (BufferPtr < BufferEnd)
		{
			uint64 DecodedCycle = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint64 ActualCycle = (DecodedCycle >> 1) + LastCycle;
			LastCycle = ActualCycle;
			if (DecodedCycle & 1ull)
			{
				EventScopeState& ScopeState = ThreadState->ScopeStack.AddDefaulted_GetRef();
				ScopeState.StartCycle = ActualCycle;
				uint16 SpecId = DecodeSpecId(BufferPtr);
				if (SpecId == 0)
				{
					uint16 DynamicEventNameId = DecodeSpecId(BufferPtr);
					check(DynamicNameToEventIdMap.Contains(DynamicEventNameId));
					ScopeState.EventTypeId = DynamicNameToEventIdMap[DynamicEventNameId];
				}
				else
				{
					check(ScopeIdToEventIdMap.Contains(SpecId));
					ScopeState.EventTypeId = ScopeIdToEventIdMap[SpecId];
				}
				Trace::FTimingProfilerEvent Event;
				Event.TimerIndex = ScopeState.EventTypeId;
				ThreadState->Timeline->AppendBeginEvent(Context.SessionContext.TimestampFromCycle(ScopeState.StartCycle), Event);
			}
			else
			{
				check(ThreadState->ScopeStack.Num());
				ThreadState->ScopeStack.Pop();
				ThreadState->Timeline->AppendEndEvent(Context.SessionContext.TimestampFromCycle(ActualCycle));
				LatestEndCycle = ActualCycle;
			}
		}
		check(BufferPtr == BufferEnd);
		if (LatestEndCycle)
		{
			Session->UpdateDuration(Context.SessionContext.TimestampFromCycle(LatestEndCycle));
		}
		break;
	}
	}
}

TSharedRef<FCpuProfilerAnalyzer::FThreadState> FCpuProfilerAnalyzer::GetThreadState(uint32 ThreadId)
{
	if (!ThreadStatesMap.Contains(ThreadId))
	{
		TSharedRef<FThreadState> ThreadState = MakeShared<FThreadState>();
		ThreadState->Timeline = TimingProfilerProvider->EditCpuThreadTimeline(ThreadId);
		ThreadStatesMap.Add(ThreadId, ThreadState);
		ThreadProvider->EnsureThreadExists(ThreadId);
		return ThreadState;
	}
	else
	{
		return ThreadStatesMap[ThreadId];
	}
}

uint16 FCpuProfilerAnalyzer::DecodeSpecId(const uint8*& BufferPtr)
{
	uint16 SpecId = 0;
	SpecId |= *BufferPtr++;
	SpecId |= (uint16(*BufferPtr++) << 8);
	return SpecId;
}
