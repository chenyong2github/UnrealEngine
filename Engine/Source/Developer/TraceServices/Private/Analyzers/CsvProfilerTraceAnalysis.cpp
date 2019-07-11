// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "CsvProfilerTraceAnalysis.h"
#include "AnalysisServicePrivate.h"
#include "TraceServices/Model/Counters.h"
#include "TraceServices/Model/Frames.h"

FCsvProfilerAnalyzer::FCsvProfilerAnalyzer(Trace::IAnalysisSession& InSession, Trace::FCsvProfilerProvider& InCsvProfilerProvider, const Trace::IFrameProvider& InFrameProvider, const Trace::IThreadProvider& InThreadProvider)
	: Session(InSession)
	, CsvProfilerProvider(InCsvProfilerProvider)
	, FrameProvider(InFrameProvider)
	, ThreadProvider(InThreadProvider)
{
}

FCsvProfilerAnalyzer::~FCsvProfilerAnalyzer()
{
	OnAnalysisEnd();
}

void FCsvProfilerAnalyzer::OnAnalysisBegin(const FOnAnalysisContext& Context)
{
	auto& Builder = Context.InterfaceBuilder;

	Builder.RouteEvent(RouteId_RegisterCategory, "CsvProfiler", "RegisterCategory");
	Builder.RouteEvent(RouteId_DefineInlineStat, "CsvProfiler", "DefineInlineStat");
	Builder.RouteEvent(RouteId_DefineDeclaredStat, "CsvProfiler", "DefineDeclaredStat");
	Builder.RouteEvent(RouteId_BeginStat, "CsvProfiler", "BeginStat");
	Builder.RouteEvent(RouteId_EndStat, "CsvProfiler", "EndStat");
	Builder.RouteEvent(RouteId_BeginExclusiveStat, "CsvProfiler", "BeginExclusiveStat");
	Builder.RouteEvent(RouteId_EndExclusiveStat, "CsvProfiler", "EndExclusiveStat");
	Builder.RouteEvent(RouteId_CustomStatInt, "CsvProfiler", "CustomStatInt");
	Builder.RouteEvent(RouteId_CustomStatFloat, "CsvProfiler", "CustomStatFloat");
	Builder.RouteEvent(RouteId_Event, "CsvProfiler", "Event");
	Builder.RouteEvent(RouteId_Metadata, "CsvProfiler", "Metadata");
	Builder.RouteEvent(RouteId_BeginCapture, "CsvProfiler", "BeginCapture");
	Builder.RouteEvent(RouteId_EndCapture, "CsvProfiler", "EndCapture");
}

void FCsvProfilerAnalyzer::OnAnalysisEnd()
{
	for (FStatSeriesInstance* StatSeriesInstance : StatSeriesInstanceArray)
	{
		delete StatSeriesInstance;
	}
	StatSeriesInstanceArray.Empty();
	for (FStatSeriesDefinition* StatSeriesDefinition : StatSeriesDefinitionArray)
	{
		delete StatSeriesDefinition;
	}
	StatSeriesDefinitionArray.Empty();
	StatSeriesMap.Empty();
	StatSeriesStringMap.Empty();
	for (auto& KV : ThreadStatesMap)
	{
		FThreadState* ThreadState = KV.Value;
		delete ThreadState;
	}
	ThreadStatesMap.Empty();
}

void FCsvProfilerAnalyzer::OnEvent(uint16 RouteId, const FOnEventContext& Context)
{
	Trace::FAnalysisSessionEditScope _(Session);

	const auto& EventData = Context.EventData;
	switch (RouteId)
	{
	case RouteId_RegisterCategory:
	{
		int32 CategoryIndex = EventData.GetValue<int32>("Index");
		const TCHAR* Name = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		CategoryMap.Add(CategoryIndex, Session.StoreString(Name));;
		break;
	}
	case RouteId_DefineInlineStat:
	{
		uint64 StatId = EventData.GetValue<uint64>("StatId");
		int32 CategoryIndex = EventData.GetValue<int32>("CategoryIndex");
		DefineStatSeries(StatId, ANSI_TO_TCHAR(reinterpret_cast<const ANSICHAR*>(EventData.GetAttachment())), CategoryIndex, true);
		break;
	}
	case RouteId_DefineDeclaredStat:
	{
		uint64 StatId = EventData.GetValue<uint64>("StatId");
		int32 CategoryIndex = EventData.GetValue<int32>("CategoryIndex");
		DefineStatSeries(StatId, reinterpret_cast<const TCHAR*>(EventData.GetAttachment()), CategoryIndex, false);
		break;
	}
	case RouteId_BeginStat:
	{
		HandleMarkerEvent(Context, false, true);
		break;
	}
	case RouteId_EndStat:
	{
		HandleMarkerEvent(Context, false, false);
		break;
	}
	case RouteId_BeginExclusiveStat:
	{
		HandleMarkerEvent(Context, true, true);
		break;
	}
	case RouteId_EndExclusiveStat:
	{
		HandleMarkerEvent(Context, true, false);
		break;
	}
	case RouteId_CustomStatInt:
	{
		HandleCustomStatEvent(Context, false);
		break;
	}
	case RouteId_CustomStatFloat:
	{
		HandleCustomStatEvent(Context, true);
		break;
	}
	case RouteId_Event:
	{
		HandleEventEvent(Context);
		break;
	}
	case RouteId_Metadata:
	{
		const TCHAR* Key = reinterpret_cast<const TCHAR*>(EventData.GetAttachment());
		const TCHAR* Value = reinterpret_cast<const TCHAR*>(EventData.GetAttachment() + EventData.GetValue<uint16>("ValueOffset"));
		CsvProfilerProvider.SetMetadata(Session.StoreString(Key), Session.StoreString(Value));
		break;
	}
	case RouteId_BeginCapture:
	{
		RenderThreadId = EventData.GetValue<uint32>("RenderThreadId");
		RHIThreadId = EventData.GetValue<uint32>("RHIThreadId");
		int32 CaptureStartFrame = GetFrameNumberForTimestamp(TraceFrameType_Game, Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		bEnableCounts = EventData.GetValue<bool>("EnableCounts");
		const TCHAR* Filename = Session.StoreString(reinterpret_cast<const TCHAR*>(EventData.GetAttachment()));
		CsvProfilerProvider.StartCapture(Filename, CaptureStartFrame);
		break;
	}
	case RouteId_EndCapture:
	{
		int32 CaptureEndFrame = GetFrameNumberForTimestamp(TraceFrameType_Game, Context.SessionContext.TimestampFromCycle(EventData.GetValue<uint64>("Cycle")));
		CsvProfilerProvider.EndCapture(CaptureEndFrame);
	}
	}
}

FCsvProfilerAnalyzer::FThreadState& FCsvProfilerAnalyzer::GetThreadState(uint32 ThreadId)
{
	FThreadState** FindIt = ThreadStatesMap.Find(ThreadId);
	if (FindIt)
	{
		return **FindIt;
	}
	FThreadState* ThreadState = new FThreadState();
	if (ThreadId == RenderThreadId || ThreadId == RHIThreadId)
	{
		ThreadState->FrameType = TraceFrameType_Rendering;
	}
	ThreadState->ThreadName = ThreadId == RenderThreadId ? TEXT("RenderThread") : ThreadProvider.GetThreadName(ThreadId);
	ThreadStatesMap.Add(ThreadId, ThreadState);
	return *ThreadState;
}

FCsvProfilerAnalyzer::FStatSeriesDefinition* FCsvProfilerAnalyzer::CreateStatSeries(const TCHAR* Name, int32 CategoryIndex)
{
	FStatSeriesDefinition* StatSeries = new FStatSeriesDefinition();
	StatSeries->Name = Session.StoreString(Name);
	StatSeries->CategoryIndex = CategoryIndex;
	StatSeries->ColumnIndex = StatSeriesDefinitionArray.Num();
	StatSeriesDefinitionArray.Add(StatSeries);
	return StatSeries;
}

void FCsvProfilerAnalyzer::DefineStatSeries(uint64 StatId, const TCHAR* Name, int32 CategoryIndex, bool bIsInline)
{
	FStatSeriesDefinition** FindIt = StatSeriesMap.Find(StatId);
	if (bIsInline && !FindIt)
	{
		TTuple<int32, FString> Key = TTuple<int32, FString>(CategoryIndex, Name);
		FindIt = StatSeriesStringMap.Find(Key);
		if (FindIt)
		{
			StatSeriesMap.Add(StatId, *FindIt);
		}
	}
	if (!FindIt)
	{
		FStatSeriesDefinition* StatSeries = CreateStatSeries(Name, CategoryIndex);
		StatSeriesMap.Add(StatId, StatSeries);
		if (bIsInline)
		{
			TTuple<int32, FString> Key = TTuple<int32, FString>(CategoryIndex, Name);
			StatSeriesStringMap.Add(Key, StatSeries);
		}
	}
}

const TCHAR* FCsvProfilerAnalyzer::GetStatSeriesName(const FStatSeriesDefinition* Definition, Trace::ECsvStatSeriesType Type, FThreadState& ThreadState, bool bIsCount)
{
	FString Name = Definition->Name;
	if (Type == Trace::CsvStatSeriesType_Timer || Type == Trace::CsvStatSeriesType_ExclusiveTimer || bIsCount)
	{
		// Add a /<Threadname> prefix
		Name = ThreadState.ThreadName + TEXT("/") + Name;
	}

	if (Definition->CategoryIndex > 0)
	{
		// Categorized stats are prefixed with <CATEGORY>/
		Name = FString(CategoryMap[Definition->CategoryIndex]) + TEXT("/") + Name;
	}
	else if (Type == Trace::CsvStatSeriesType_ExclusiveTimer)
	{
		Name = FString(TEXT("Exclusive/")) + Name;
	}

	if (bIsCount)
	{
		// Add a counts prefix
		Name = TEXT("COUNTS/") + Name;
	}

	return Session.StoreString(*Name);
}

FCsvProfilerAnalyzer::FStatSeriesInstance* FCsvProfilerAnalyzer::GetStatSeries(uint64 StatId, Trace::ECsvStatSeriesType Type, FThreadState& ThreadState)
{
	FStatSeriesDefinition* Definition;
	FStatSeriesDefinition** FindIt = StatSeriesMap.Find(StatId);
	if (!FindIt)
	{
		Definition = CreateStatSeries(*FString::Printf(TEXT("[undefined%d]"), UndefinedStatSeriesCount++), 0);
		StatSeriesMap.Add(StatId, Definition);
	}
	else
	{
		Definition = *FindIt;
	}

	if (ThreadState.StatSeries.Num() <= Definition->ColumnIndex)
	{
		ThreadState.StatSeries.AddZeroed(Definition->ColumnIndex + 1 - ThreadState.StatSeries.Num());
	}
	FStatSeriesInstance* Instance = ThreadState.StatSeries[Definition->ColumnIndex];
	if (Instance)
	{
		return Instance;
	}

	Instance = new FStatSeriesInstance();
	StatSeriesInstanceArray.Add(Instance);
	ThreadState.StatSeries[Definition->ColumnIndex] = Instance;
	Instance->ProviderHandle = CsvProfilerProvider.AddSeries(GetStatSeriesName(Definition, Type, ThreadState, false), Type);
	Instance->ProviderCountHandle = CsvProfilerProvider.AddSeries(GetStatSeriesName(Definition, Type, ThreadState, true), Trace::CsvStatSeriesType_CustomStatInt);

	return Instance;
}

void FCsvProfilerAnalyzer::HandleMarkerEvent(const FOnEventContext& Context, bool bIsExclusive, bool bIsBegin)
{
	FThreadState& ThreadState = GetThreadState(Context.EventData.GetValue<uint32>("ThreadId"));
	uint64 StatId = Context.EventData.GetValue<uint64>("StatId");
	FTimingMarker Marker;
	Marker.StatSeries = GetStatSeries(StatId, Trace::CsvStatSeriesType_Timer, ThreadState);
	Marker.bIsBegin = bIsBegin;
	Marker.bIsExclusive = bIsExclusive;
	Marker.Cycle = Context.EventData.GetValue<uint64>("Cycle");
	HandleMarker(Context, ThreadState, Marker);
}

void FCsvProfilerAnalyzer::HandleMarker(const FOnEventContext& Context, FThreadState& ThreadState, const FTimingMarker& Marker)
{
	// Handle exclusive markers. This may insert an additional marker before this one
	bool bInsertExtraMarker = false;
	FTimingMarker InsertedMarker;
	if (Marker.bIsExclusive & !Marker.bIsExclusiveInsertedMarker)
	{
		if (Marker.bIsBegin)
		{
			if (ThreadState.ExclusiveMarkerStack.Num() > 0)
			{
				// Insert an artificial end marker to end the previous marker on the stack at the same timestamp
				InsertedMarker = ThreadState.ExclusiveMarkerStack.Last();
				InsertedMarker.bIsBegin = false;
				InsertedMarker.bIsExclusiveInsertedMarker = true;
				InsertedMarker.Cycle = Marker.Cycle;

				bInsertExtraMarker = true;
			}
			ThreadState.ExclusiveMarkerStack.Add(Marker);
		}
		else
		{
			if (ThreadState.ExclusiveMarkerStack.Num() > 0)
			{
				ThreadState.ExclusiveMarkerStack.Pop(false);
				if (ThreadState.ExclusiveMarkerStack.Num() > 0)
				{
					// Insert an artificial begin marker to resume the marker on the stack at the same timestamp
					InsertedMarker = ThreadState.ExclusiveMarkerStack.Last();
					InsertedMarker.bIsBegin = true;
					InsertedMarker.bIsExclusiveInsertedMarker = true;
					InsertedMarker.Cycle = Marker.Cycle;

					bInsertExtraMarker = true;
				}
			}
		}
	}
	if (bInsertExtraMarker)
	{
		HandleMarker(Context, ThreadState, InsertedMarker);
	}
	
	int32 FrameNumber = GetFrameNumberForTimestamp(ThreadState.FrameType, Context.SessionContext.TimestampFromCycle(Marker.Cycle));
	if (Marker.bIsBegin)
	{
		ThreadState.MarkerStack.Push(Marker);
	}
	else
	{
		// Markers might not match up if they were truncated mid-frame, so we need to be robust to that
		if (ThreadState.MarkerStack.Num() > 0)
		{
			// Find the start marker (might not actually be top of the stack, e.g if begin/end for two overlapping stats are independent)
			bool bFoundStart = false;
			FTimingMarker StartMarker;

			for (int j = ThreadState.MarkerStack.Num() - 1; j >= 0; j--)
			{
				if (ThreadState.MarkerStack[j].StatSeries == Marker.StatSeries) // Note: only works with scopes!
				{
					StartMarker = ThreadState.MarkerStack[j];
					ThreadState.MarkerStack.RemoveAt(j, 1, false);
					bFoundStart = true;
					break;
				}
			}

			// TODO: if bFoundStart is false, this stat _never_ gets processed. Could we add it to a persistent list so it's considered next time?
			// Example where this could go wrong: staggered/overlapping exclusive stats ( e.g Abegin, Bbegin, AEnd, BEnd ), where processing ends after AEnd
			// AEnd would be missing 
			if (FrameNumber >= 0 && bFoundStart)
			{
				check(Marker.StatSeries == StartMarker.StatSeries);
				check(Marker.Cycle >= StartMarker.Cycle);
				if (Marker.Cycle > StartMarker.Cycle)
				{
					uint64 ElapsedCycles = Marker.Cycle - StartMarker.Cycle;
					CsvProfilerProvider.SetTimerValue(Marker.StatSeries->ProviderHandle, FrameNumber, Context.SessionContext.DurationFromCycleCount(ElapsedCycles) * 1000.0);

					// Add the COUNT/ series if enabled. Ignore artificial markers (inserted above)
					if (bEnableCounts && !Marker.bIsExclusiveInsertedMarker)
					{
						CsvProfilerProvider.SetCustomStatValue(Marker.StatSeries->ProviderCountHandle, FrameNumber, Trace::CsvOpType_Accumulate, 1);
					}
				}
			}
		}
	}
}

void FCsvProfilerAnalyzer::HandleCustomStatEvent(const FOnEventContext& Context, bool bIsFloat)
{
	FThreadState& ThreadState = GetThreadState(Context.EventData.GetValue<uint32>("ThreadId"));
	FStatSeriesInstance* StatSeries = GetStatSeries(Context.EventData.GetValue<uint64>("StatId"), Trace::CsvStatSeriesType_CustomStatFloat, ThreadState);
	Trace::ECsvOpType OpType = static_cast<Trace::ECsvOpType>(Context.EventData.GetValue<uint8>("OpType"));
	uint64 Cycle = Context.EventData.GetValue<uint64>("Cycle");
	int32 FrameNumber = GetFrameNumberForTimestamp(ThreadState.FrameType, Context.SessionContext.TimestampFromCycle(Cycle));
	if (FrameNumber >= 0)
	{
		if (bIsFloat)
		{
			CsvProfilerProvider.SetCustomStatValue(StatSeries->ProviderHandle, FrameNumber, OpType, Context.EventData.GetValue<float>("Value"));
		}
		else
		{
			CsvProfilerProvider.SetCustomStatValue(StatSeries->ProviderHandle, FrameNumber, OpType, Context.EventData.GetValue<int32>("Value"));
		}

		// Add the COUNT/ series if enabled
		if (bEnableCounts)
		{
			CsvProfilerProvider.SetCustomStatValue(StatSeries->ProviderCountHandle, FrameNumber, Trace::CsvOpType_Accumulate, 1);
		}
	}
}

void FCsvProfilerAnalyzer::HandleEventEvent(const FOnEventContext& Context)
{
	FThreadState& ThreadState = GetThreadState(Context.EventData.GetValue<uint32>("ThreadId"));
	uint64 Cycle = Context.EventData.GetValue<uint64>("Cycle");
	int32 FrameNumber = GetFrameNumberForTimestamp(ThreadState.FrameType, Context.SessionContext.TimestampFromCycle(Cycle));
	if (FrameNumber >= 0)
	{
		FString EventText = reinterpret_cast<const TCHAR*>(Context.EventData.GetAttachment());
		int32 CategoryIndex = Context.EventData.GetValue<int32>("CategoryIndex");
		if (CategoryIndex > 0)
		{
			EventText = FString(CategoryMap[CategoryIndex]) + TEXT("/") + EventText;
		}
		CsvProfilerProvider.AddEvent(FrameNumber, Session.StoreString(*EventText));
	}
}

int32 FCsvProfilerAnalyzer::GetFrameNumberForTimestamp(ETraceFrameType FrameType, double Timestamp) const
{
	const TArray<double>& FrameStartTimes = FrameProvider.GetFrameStartTimes(FrameType);

	if (FrameStartTimes.Num() == 0 || Timestamp < FrameStartTimes[0])
	{
		return 0;
	}

	uint32 Index = static_cast<uint32>(Algo::LowerBound(FrameStartTimes, Timestamp));
	return Index + 1;
}
