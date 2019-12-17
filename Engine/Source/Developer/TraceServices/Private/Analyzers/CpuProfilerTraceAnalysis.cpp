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

FCpuProfilerAnalyzer::~FCpuProfilerAnalyzer()
{
	for (auto& KV : ThreadStatesMap)
	{
		FThreadState* ThreadState = KV.Value;
		delete ThreadState;
	}
}

void FCpuProfilerAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_EventSpec, "CpuProfiler", "EventSpec");
	Builder.RouteEvent(RouteId_EventBatch, "CpuProfiler", "EventBatch");
	Builder.RouteEvent(RouteId_EndCapture, "CpuProfiler", "EndCapture");
}

bool FCpuProfilerAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint32 Id = EventData.GetValue<uint32>("Id");
		uint8 CharSize = EventData.GetValue<uint8>("CharSize");
		if (CharSize == sizeof(ANSICHAR))
		{
			DefineScope(Id, Session.StoreString(StringCast<TCHAR>(reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment())).Get()));
		}
		else if (CharSize == 0 || CharSize == sizeof(TCHAR)) // 0 for backwards compatibility
		{
			DefineScope(Id, Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment())));
		}
		break;
	}
	case RouteId_EventBatch:
	case RouteId_EndCapture:
	{
		TotalEventSize += EventData.GetAttachmentSize();
		uint32 ThreadId = EventData.GetValue<uint32>("ThreadId");
		FThreadState& ThreadState = GetThreadState(ThreadId);
		uint64 LastCycle = ThreadState.LastCycle;
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
				EventScopeState& ScopeState = ThreadState.ScopeStack.AddDefaulted_GetRef();
				ScopeState.StartCycle = ActualCycle;
				uint32 SpecId = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
				uint32* FindIt = ScopeIdToEventIdMap.Find(SpecId);
				if (!FindIt)
				{
					ScopeState.EventTypeId = ScopeIdToEventIdMap.Add(SpecId, TimingProfilerProvider.AddCpuTimer(TEXT("<unknown>")));
				}
				else
				{
					ScopeState.EventTypeId = *FindIt;
				}
				Trace::FTimingProfilerEvent Event;
				Event.TimerIndex = ScopeState.EventTypeId;
				ThreadState.Timeline->AppendBeginEvent(Context.SessionContext.TimestampFromCycle(ScopeState.StartCycle), Event);
				++TotalScopeCount;
			}
			else if (ThreadState.ScopeStack.Num())
			{
				ThreadState.ScopeStack.Pop();
				ThreadState.Timeline->AppendEndEvent(Context.SessionContext.TimestampFromCycle(ActualCycle));
			}
		}
		check(BufferPtr == BufferEnd);
		if (LastCycle)
		{
			double LastTimestamp = Context.SessionContext.TimestampFromCycle(LastCycle);
			Session.UpdateDurationSeconds(LastTimestamp);
			if (RouteId == RouteId_EndCapture)
			{

				while (ThreadState.ScopeStack.Num())
				{
					ThreadState.ScopeStack.Pop();
					ThreadState.Timeline->AppendEndEvent(LastTimestamp);
				}
			}
		}
		ThreadState.LastCycle = LastCycle;
		BytesPerScope = double(TotalEventSize) / double(TotalScopeCount);
		break;
	}
	}

	return true;
}

void FCpuProfilerAnalyzer::DefineScope(uint32 Id, const TCHAR* Name)
{
	if (ScopeIdToEventIdMap.Contains(Id))
	{
		TimingProfilerProvider.SetTimerName(ScopeIdToEventIdMap[Id], Name);
		ScopeNameToEventIdMap.Add(Name, Id);
	}
	else
	{
		uint32* FindEventIdByName = ScopeNameToEventIdMap.Find(Name);
		if (FindEventIdByName)
		{
			ScopeIdToEventIdMap.Add(Id, *FindEventIdByName);
		}
		else
		{
			uint32 NewTimerId = TimingProfilerProvider.AddCpuTimer(Name);
			ScopeIdToEventIdMap.Add(Id, NewTimerId);
			ScopeNameToEventIdMap.Add(Name, NewTimerId);
		}
	}
}

FCpuProfilerAnalyzer::FThreadState& FCpuProfilerAnalyzer::GetThreadState(uint32 ThreadId)
{
	FThreadState* ThreadState = ThreadStatesMap.FindRef(ThreadId);
	if (!ThreadState)
	{
		ThreadState = new FThreadState();
		ThreadState->Timeline = &TimingProfilerProvider.EditCpuThreadTimeline(ThreadId);
		ThreadStatesMap.Add(ThreadId, ThreadState);
	}
	return *ThreadState;
}

