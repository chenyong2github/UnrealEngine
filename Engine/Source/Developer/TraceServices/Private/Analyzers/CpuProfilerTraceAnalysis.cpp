// Copyright Epic Games, Inc. All Rights Reserved.
#include "CpuProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "Common/Utils.h"
#include "Model/ThreadsPrivate.h"

namespace TraceServices
{

FCpuProfilerAnalyzer::FCpuProfilerAnalyzer(IAnalysisSession& InSession, FTimingProfilerProvider& InTimingProfilerProvider, FThreadProvider& InThreadProvider)
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
	FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_EventSpec:
	{
		uint32 SpecId = EventData.GetValue<uint32>("Id");

		const TCHAR* TimerName = nullptr;
		FString Name;
		if (EventData.GetString("Name", Name))
		{
			TimerName = *Name;
		}
		else
		{
			uint8 CharSize = EventData.GetValue<uint8>("CharSize");
			if (CharSize == sizeof(ANSICHAR))
			{
				const ANSICHAR* AnsiName = reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment());
				Name = StringCast<TCHAR>(AnsiName).Get();
				TimerName = *Name;
			}
			else if (CharSize == 0 || CharSize == sizeof(TCHAR)) // 0 for backwards compatibility
			{
				TimerName = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
			}
			else
			{
				Name = FString::Printf(TEXT("<invalid %u>"), SpecId);
				TimerName = *Name;
			}
		}

		if (TimerName[0] == 0)
		{
			Name = FString::Printf(TEXT("<noname %u>"), SpecId);
			TimerName = *Name;
		}

		const TCHAR* FileName = nullptr;
		FString File;
		uint32 Line = 0;
		if (EventData.GetString("File", File) && !File.IsEmpty())
		{
			FileName = *File;
			Line = EventData.GetValue<uint32>("Line");
		}

		constexpr bool bMergeByName = true;
		DefineTimer(SpecId, Session.StoreString(TimerName), FileName, Line, bMergeByName);
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
		ThreadState.LastCycle = ~0ull;
		break;
	}

	case RouteId_EventBatch:
	case RouteId_EndCapture:
	{
		const uint32 ThreadId = FTraceAnalyzerUtils::GetThreadIdField(Context);
		FThreadState& ThreadState = GetThreadState(ThreadId);

		if (ThreadState.LastCycle == ~0ull)
		{
			// Ignore timing events received after EndThread.
			break;
		}

		TArrayView<const uint8> DataView = FTraceAnalyzerUtils::LegacyAttachmentArray("Data", Context);
		TotalEventSize += DataView.Num();
		const uint32 BufferSize = DataView.Num();
		const uint8* BufferPtr = DataView.GetData();

		uint64 LastCycle = ProcessBuffer(Context.EventTime, ThreadState, BufferPtr, BufferSize);
		if (LastCycle != 0)
		{
			double LastTimestamp = Context.EventTime.AsSeconds(LastCycle);
			Session.UpdateDurationSeconds(LastTimestamp);
			if (RouteId == RouteId_EndCapture)
			{
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
		if (Style == EStyle::EnterScope)
		{
			OnCpuScopeEnter(Context);
		}
		else
		{
			OnCpuScopeLeave(Context);
		}
		break;
	}

	return true;
}

uint64 FCpuProfilerAnalyzer::ProcessBuffer(const FEventTime& EventTime, FThreadState& ThreadState, const uint8* BufferPtr, uint32 BufferSize)
{
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

			double PendingTime = EventTime.AsSeconds(PendingCycle);
			if (bEnter)
			{
				FTimingProfilerEvent Event;
				Event.TimerIndex = PendingCursor->TimerId;
				ThreadState.Timeline->AppendBeginEvent(PendingTime, Event);
			}
			else
			{
				ThreadState.Timeline->AppendEndEvent(PendingTime);
			}
		}

		if (DecodedCycle & 1ull)
		{
			uint32 SpecId = FTraceAnalyzerUtils::Decode7bit(BufferPtr);
			uint32* FindIt = SpecIdToTimerIdMap.Find(SpecId);
			uint32 TimerId;
			if (!FindIt)
			{
				// Adds a timer with an "unknown" name.
				const TCHAR* TimerName = Session.StoreString(*FString::Printf(TEXT("<unknown %u>"), SpecId));
				// The name might be updated when an EventSpec event is received (for this SpecId),
				// so we do not want to merge by name all the unknown timers.
				constexpr bool bMergeByName = false;
				TimerId = DefineTimer(SpecId, TimerName, nullptr, 0, bMergeByName);
			}
			else
			{
				TimerId = *FindIt;
			}

			FEventScopeState& ScopeState = ThreadState.ScopeStack.AddDefaulted_GetRef();
			ScopeState.StartCycle = ActualCycle;
			ScopeState.EventTypeId = TimerId;

			FTimingProfilerEvent Event;
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
			double PendingTime = EventTime.AsSeconds(PendingCycle);
			ThreadState.Timeline->AppendEndEvent(PendingTime);
		}
		else
		{
			FTimingProfilerEvent Event;
			Event.TimerIndex = PendingCursor->TimerId;
			double PendingTime = EventTime.AsSeconds(PendingCycle);
			ThreadState.Timeline->AppendBeginEvent(PendingTime, Event);
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

	uint32 TimerId;
	uint32* TimerIdIter = SpecIdToTimerIdMap.Find(SpecId);
	if (TimerIdIter)
	{
		TimerId = *TimerIdIter;
	}
	else
	{
		FString ScopeName;
		ScopeName += Context.EventData.GetTypeInfo().GetName();
		constexpr bool bMergeByName = true;
		TimerId = DefineTimer(SpecId, Session.StoreString(*ScopeName), nullptr, 0, bMergeByName);
	}

	TArray<uint8> CborData;
	Context.EventData.SerializeToCbor(CborData);
	TimerId = TimingProfilerProvider.AddMetadata(TimerId, MoveTemp(CborData));

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

uint32 FCpuProfilerAnalyzer::DefineTimer(uint32 SpecId, const TCHAR* Name, const TCHAR* File, uint32 Line, bool bMergeByName)
{
	// The cpu scoped events (timers) will be merged by name.
	// Ex.: If there are multiple timers defined in code with same name,
	//      those will appear in Insights as a single timer.

	// Check if a timer with same name was already defined.
	uint32* FindTimerIdByName = bMergeByName ? ScopeNameToTimerIdMap.Find(Name) : nullptr;
	if (FindTimerIdByName)
	{
		// Yes, a timer with same name was already defined.
		// Check if SpecId is already mapped to timer.
		uint32* FindTimerId = SpecIdToTimerIdMap.Find(SpecId);
		if (FindTimerId)
		{
			// Yes, SpecId was already mapped to a timer (ex. as an <unknown> timer).
			// Update name for mapped timer.
			TimingProfilerProvider.SetTimerNameAndLocation(*FindTimerId, Name, File, Line);
			// In this case, we do not remap the SpecId to the previously defined timer with same name.
			// This is becasue the two timers are already used in timelines.
			// So we will continue to use separate timers, even if those have same name.
			return *FindTimerId;
		}
		else
		{
			// Map this SpecId to the previously defined timer with same name.
			SpecIdToTimerIdMap.Add(SpecId, *FindTimerIdByName);
			return *FindTimerIdByName;
		}
	}
	else
	{
		// No, a timer with same name was not defined (or we do not want to merge by name).
		// Check if SpecId is already mapped to timer.
		uint32* FindTimerId = SpecIdToTimerIdMap.Find(SpecId);
		if (FindTimerId)
		{
			// Yes, SpecId was already mapped to a timer (ex. as an <unknown> timer).
			// Update name for mapped timer.
			TimingProfilerProvider.SetTimerNameAndLocation(*FindTimerId, Name, File, Line);
			if (bMergeByName)
			{
				// Map the name to the timer.
				ScopeNameToTimerIdMap.Add(Name, *FindTimerId);
			}
			return *FindTimerId;
		}
		else
		{
			// Define a new Cpu timer.
			uint32 NewTimerId = TimingProfilerProvider.AddCpuTimer(Name, File, Line);
			// Map the SpecId to the timer.
			SpecIdToTimerIdMap.Add(SpecId, NewTimerId);
			if (bMergeByName)
			{
				// Map the name to the timer.
				ScopeNameToTimerIdMap.Add(Name, NewTimerId);
			}
			return NewTimerId;
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

} // namespace TraceServices
