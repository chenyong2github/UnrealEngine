// Copyright Epic Games, Inc. All Rights Reserved.
#include "CpuProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "Model/ThreadsPrivate.h"

FCpuProfilerAnalyzer::FCpuProfilerAnalyzer(Trace::IAnalysisSession& InSession, Trace::FTimingProfilerProvider& InTimingProfilerProvider, Trace::FThreadProvider& InThreadProvider)
	: Session(InSession)
	, TimingProfilerProvider(InTimingProfilerProvider)
	, ThreadProvider(InThreadProvider)
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

	Builder.RouteLoggerEvents(RouteId_CpuScope, "Cpu", true);
	Builder.RouteEvent(RouteId_EventSpec, "CpuProfiler", "EventSpec");
	Builder.RouteEvent(RouteId_EventBatch, "CpuProfiler", "EventBatch");
	Builder.RouteEvent(RouteId_EndThread, "CpuProfiler", "EndThread");
	Builder.RouteEvent(RouteId_EndCapture, "CpuProfiler", "EndCapture");
}

bool FCpuProfilerAnalyzer::OnEvent(uint16 RouteId, EStyle Style, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint32 SpecId = EventData.GetValue<uint32>("Id");
		uint8 CharSize = EventData.GetValue<uint8>("CharSize");
		if (CharSize == sizeof(ANSICHAR))
		{
			DefineScope(SpecId, Session.StoreString(StringCast<TCHAR>(reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment())).Get()));
		}
		else if (CharSize == 0 || CharSize == sizeof(TCHAR)) // 0 for backwards compatibility
		{
			DefineScope(SpecId, Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment())));
		}
		break;
	}
	case RouteId_EndThread:
	{
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		FThreadState& ThreadState = GetThreadState(ThreadId);
		ensure(ThreadState.LastCycle != 0 || ThreadState.ScopeStack.Num() == 0);
		if (ThreadState.LastCycle != 0)
		{
			double Timestamp = Context.EventTime.AsSeconds(ThreadState.LastCycle);
			Session.UpdateDurationSeconds(Timestamp);
			while (ThreadState.ScopeStack.Num())
			{
				ThreadState.ScopeStack.Pop();
				ThreadState.Timeline->AppendEndEvent(Timestamp);
			}
		}
		ThreadStatesMap.Remove(ThreadId);
	}
	case RouteId_EventBatch:
	case RouteId_EndCapture:
	{
		TotalEventSize += EventData.GetAttachmentSize();
		uint32 BufferSize = EventData.GetAttachmentSize();
		const uint8* BufferPtr = EventData.GetAttachment();
		uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		if (uint64 LastCycle = ProcessBuffer(Context.EventTime, ThreadId, BufferPtr, BufferSize))
		{
			double LastTimestamp = Context.EventTime.AsSeconds(LastCycle);
			Session.UpdateDurationSeconds(LastTimestamp);
			if (RouteId == RouteId_EndCapture)
			{
				FThreadState& ThreadState = GetThreadState(ThreadId);
				for (int32 i = ThreadState.ScopeStack.Num(); i--;)
				{
					ThreadState.ScopeStack.Pop();
					ThreadState.Timeline->AppendEndEvent(LastTimestamp);
				}
			}
		}
		BytesPerScope = double(TotalEventSize) / double(TotalScopeCount);
		break;
	}
	case RouteId_CpuScope:
		(Style == EStyle::EnterScope) ? OnCpuScopeEnter(Context) : OnCpuScopeLeave(Context);
		break;
	}

	return true;
}

uint64 FCpuProfilerAnalyzer::ProcessBuffer(const FEventTime& EventTime, uint32 ThreadId, const uint8* BufferPtr, uint32 BufferSize)
{
	FThreadState& ThreadState = GetThreadState(ThreadId);
	uint64 LastCycle = ThreadState.LastCycle;

	int32 RemainingPending = ThreadState.PendingEvents.Num();
	const FPendingEvent* PendingCursor = ThreadState.PendingEvents.GetData();

	const uint8* BufferEnd = BufferPtr + BufferSize;
	while (BufferPtr < BufferEnd)
	{
		uint64 DecodedCycle = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
		uint64 ActualCycle = (DecodedCycle >> 1);

		// ActualCycle larger or equal to LastCycle means we have a new
		// base value.
		if (ActualCycle < LastCycle)
		{
			ActualCycle += LastCycle;
		}

		// If we late connect we will be joining the cycle stream mid-flow and
		// will have missed out on it's base timestamp. Reconstruct it here.
		check(EventTime.GetTimestamp() == 0);
		uint64 BaseCycle = EventTime.AsCycle64();
		if (ActualCycle < BaseCycle)
		{
			ActualCycle += BaseCycle;
		}

		// Dispatch pending events that are younger than the one we've just decoded
		for (; RemainingPending > 0; RemainingPending--, PendingCursor++)
		{
			bool bEnter = true;
			uint64 PendingCycle = PendingCursor->Cycle;
			if (int64(PendingCycle) < 0)
			{
				PendingCycle = ~PendingCycle;
				bEnter = false;
			}

			if (PendingCycle > ActualCycle)
			{
				break;
			}

			if (PendingCycle < LastCycle)
			{
				PendingCycle = LastCycle;
			}

			double Time = EventTime.AsSeconds(PendingCycle);
			if (bEnter)
			{
				Trace::FTimingProfilerEvent Event;
				Event.TimerIndex = PendingCursor->TimerId;
				ThreadState.Timeline->AppendBeginEvent(Time, Event);
			}
			else
			{
				ThreadState.Timeline->AppendEndEvent(Time);
			}
		}

		if (DecodedCycle & 1ull)
		{
			uint32 SpecId = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint32* FindIt = SpecIdToTimerIdMap.Find(SpecId);
			uint32 TimerId;
			if (!FindIt)
			{
				TimerId = SpecIdToTimerIdMap.Add(SpecId, TimingProfilerProvider.AddCpuTimer(TEXT("<unknown>")));
			}
			else
			{
				TimerId = *FindIt;
			}

			FEventScopeState& ScopeState = ThreadState.ScopeStack.AddDefaulted_GetRef();
			ScopeState.StartCycle = ActualCycle;
			ScopeState.EventTypeId = TimerId;

			Trace::FTimingProfilerEvent Event;
			Event.TimerIndex = TimerId;
			double ActualTime = EventTime.AsSeconds(ActualCycle);
			ThreadState.Timeline->AppendBeginEvent(ActualTime, Event);
			++TotalScopeCount;
		}
		else
		{
			// If we receive mismatched end events ignore them for now.
			// This can happen for example because tracing connects to the store after events were traced. Those events can be lost.
			if (ThreadState.ScopeStack.Num() > 0)
			{
				ThreadState.ScopeStack.Pop();
				double ActualTime = EventTime.AsSeconds(ActualCycle);
				ThreadState.Timeline->AppendEndEvent(ActualTime);
			}
		}

		LastCycle = ActualCycle;
	}
	check(BufferPtr == BufferEnd);

	// Dispatch remaining pending events.
	for (; RemainingPending > 0; RemainingPending--, PendingCursor++)
	{
		uint64 PendingCycle = PendingCursor->Cycle;
		if (int64(PendingCycle) < 0)
		{
			PendingCycle = ~PendingCycle;
			double Time = EventTime.AsSeconds(PendingCycle);
			ThreadState.Timeline->AppendEndEvent(Time);
		}
		else
		{
			Trace::FTimingProfilerEvent Event;
			Event.TimerIndex = PendingCursor->TimerId;
			double Time = EventTime.AsSeconds(PendingCycle);
			ThreadState.Timeline->AppendBeginEvent(Time, Event);
		}
	}

	ThreadState.PendingEvents.Reset();
	ThreadState.LastCycle = LastCycle;
	return LastCycle;
}

void FCpuProfilerAnalyzer::OnCpuScopeEnter(const FOnEventContext& Context)
{
	if (Context.EventTime.GetTimestamp() == 0)
	{
		return;
	}

	uint32 ThreadId = Context.ThreadInfo.GetId();
	FThreadState& ThreadState = GetThreadState(ThreadId);

	uint32 SpecId = Context.EventData.GetTypeInfo().GetId();
	SpecId = ~SpecId; // to keep out of the way of normal spec IDs.
	uint32* TimerIdIter = SpecIdToTimerIdMap.Find(SpecId);
	if (TimerIdIter == nullptr)
	{
		FString ScopeName;
		ScopeName += Context.EventData.GetTypeInfo().GetName();
		DefineScope(SpecId, Session.StoreString(*ScopeName));
		TimerIdIter = SpecIdToTimerIdMap.Find(SpecId);
	}

	TArray<uint8> CborData;
	Context.EventData.SerializeToCbor(CborData);
	uint32 TimerId = TimingProfilerProvider.AddMetadata(*TimerIdIter, MoveTemp(CborData));

	uint64 Cycle = Context.EventTime.AsCycle64();
	ThreadState.PendingEvents.Add({Cycle, TimerId});
}

void FCpuProfilerAnalyzer::OnCpuScopeLeave(const FOnEventContext& Context)
{
	if (Context.EventTime.GetTimestamp() == 0)
	{
		return;
	}

	uint32 ThreadId = Context.ThreadInfo.GetId();
	FThreadState& ThreadState = GetThreadState(ThreadId);

	uint64 Cycle = Context.EventTime.AsCycle64();
	ThreadState.PendingEvents.Add({~Cycle});
}

void FCpuProfilerAnalyzer::DefineScope(uint32 SpecId, const TCHAR* Name)
{
	uint32* FindTimerIdByName = ScopeNameToTimerIdMap.Find(Name);
	if (FindTimerIdByName)
	{
		SpecIdToTimerIdMap.Add(SpecId, *FindTimerIdByName);
	}
	else
	{
		uint32* FindTimerId = SpecIdToTimerIdMap.Find(SpecId);
		if (FindTimerId)
		{
			TimingProfilerProvider.SetTimerName(*FindTimerId, Name);
			ScopeNameToTimerIdMap.Add(Name, *FindTimerId);
		}
		else
		{
			uint32 NewTimerId = TimingProfilerProvider.AddCpuTimer(Name);
			SpecIdToTimerIdMap.Add(SpecId, NewTimerId);
			ScopeNameToTimerIdMap.Add(Name, NewTimerId);
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

		// Just in case the rest of Insight's reporting/analysis doesn't know about
		// this thread, we'll explicitly add it. For fault tolerance.
		ThreadProvider.AddThread(ThreadId, nullptr, TPri_Normal);
	}
	return *ThreadState;
}
