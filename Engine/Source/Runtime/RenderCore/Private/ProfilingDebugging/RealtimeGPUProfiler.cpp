// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/TracingProfiler.h"
#include "RenderCore.h"

// Only exposed for debugging. Disabling this carries a severe performance penalty
#define RENDER_QUERY_POOLING_ENABLED 1

#if HAS_GPU_STATS 

CSV_DEFINE_CATEGORY_MODULE(RENDERCORE_API, GPU, true);

static TAutoConsoleVariable<int> CVarGPUStatsEnabled(
	TEXT("r.GPUStatsEnabled"),
	1,
	TEXT("Enables or disables GPU stat recording"));


static TAutoConsoleVariable<int> CVarGPUStatsMaxQueriesPerFrame(
	TEXT("r.GPUStatsMaxQueriesPerFrame"),
	-1,
	TEXT("Limits the number of timestamps allocated per frame. -1 = no limit"), 
	ECVF_RenderThreadSafe);


static TAutoConsoleVariable<int> CVarGPUCsvStatsEnabled(
	TEXT("r.GPUCsvStatsEnabled"),
	0,
	TEXT("Enables or disables GPU stat recording to CSVs"));

DECLARE_GPU_STAT_NAMED( Total, TEXT("[TOTAL]") );

static TAutoConsoleVariable<int> CVarGPUTracingStatsEnabled(
	TEXT("r.GPUTracingStatsEnabled"),
	1,
	TEXT("Enables or disables GPU stat recording to tracing profiler"));

static TAutoConsoleVariable<int> CVarGPUStatsChildTimesIncluded(
	TEXT("r.GPUStatsChildTimesIncluded"),
	0,
	TEXT("If this is enabled, the child stat timings will be included in their parents' times.\n")
	TEXT("This presents problems for non-hierarchical stats if we're expecting them to add up\n")
	TEXT("to the total GPU time, so we probably want this disabled.\n")
);

#endif //HAS_GPU_STATS


#if WANTS_DRAW_MESH_EVENTS

template<typename TRHICmdList>
void TDrawEvent<TRHICmdList>::Start(TRHICmdList& InRHICmdList, FColor Color, const TCHAR* Fmt, ...)
{
	check(IsInParallelRenderingThread() || IsInRHIThread());
	{
		va_list ptr;
		va_start(ptr, Fmt);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, UE_ARRAY_COUNT(TempStr), Fmt, ptr);
		InRHICmdList.PushEvent(TempStr, Color);
		RHICmdList = &InRHICmdList;
		va_end(ptr);
	}
}

template<typename TRHICmdList>
void TDrawEvent<TRHICmdList>::Stop()
{
	if (RHICmdList)
	{
		RHICmdList->PopEvent();
		RHICmdList = NULL;
	}
}
template struct TDrawEvent<FRHICommandList>;
template struct TDrawEvent<FRHIAsyncComputeCommandList>;
template struct TDrawEvent<FRHIAsyncComputeCommandListImmediate>;

void FDrawEventRHIExecute::Start(IRHIComputeContext& InRHICommandContext, FColor Color, const TCHAR* Fmt, ...)
{
	check(IsInParallelRenderingThread() || IsInRHIThread() || (!IsRunningRHIInSeparateThread() && IsInRenderingThread()));
	{
		va_list ptr;
		va_start(ptr, Fmt);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		FCString::GetVarArgs(TempStr, UE_ARRAY_COUNT(TempStr), Fmt, ptr);
		RHICommandContext = &InRHICommandContext;
		RHICommandContext->RHIPushEvent(TempStr, Color);
		va_end(ptr);
	}
}

void FDrawEventRHIExecute::Stop()
{
	RHICommandContext->RHIPopEvent();
}

#endif // WANTS_DRAW_MESH_EVENTS

#if HAS_GPU_STATS
static const int32 NumGPUProfilerBufferedFrames = 4;

/*-----------------------------------------------------------------------------
FRealTimeGPUProfilerEvent class
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerEvent
{
public:
	static const uint64 InvalidQueryResult = 0xFFFFFFFFFFFFFFFFull;

public:
	FRealtimeGPUProfilerEvent(const FName& InName, const FName& InStatName, FRHIRenderQueryPool* RenderQueryPool, uint32& QueryCount)
		: StartResultMicroseconds(InvalidQueryResult)
		, EndResultMicroseconds(InvalidQueryResult)
		, FrameNumber(-1)
		, bInsideQuery(false)
		, bBeginQueryInFlight(false)
		, bEndQueryInFlight(false)
	{
#if STATS
		StatName = InStatName;
#endif
		Name = InName;

		const int MaxGPUQueries = CVarGPUStatsMaxQueriesPerFrame.GetValueOnRenderThread();
		if ( MaxGPUQueries == -1 || QueryCount < uint32(MaxGPUQueries) )
		{
			StartQuery = RenderQueryPool->AllocateQuery();
			check(StartQuery.GetQuery() != nullptr);
			EndQuery = RenderQueryPool->AllocateQuery();
			check(EndQuery.GetQuery() != nullptr);
			QueryCount += 2;
		}
	}

	bool HasQueriesAllocated() const 
	{ 
		return StartQuery.GetQuery() != nullptr;
	}

	void ReleaseQueries(FRHIRenderQueryPool* RenderQueryPool, FRHICommandListImmediate* RHICmdListPtr, uint32& QueryCount)
	{
		if ( HasQueriesAllocated() )
		{
			if (RHICmdListPtr)
			{
				// If we have queries in flight then get results before releasing back to the pool to avoid an ensure fail in the gnm RHI
				uint64 Temp;
				if (bBeginQueryInFlight)
				{
					RHICmdListPtr->GetRenderQueryResult(StartQuery.GetQuery(), Temp, true);
				}

				if (bEndQueryInFlight)
				{
					RHICmdListPtr->GetRenderQueryResult(EndQuery.GetQuery(), Temp, true);
				}
			}
			StartQuery.ReleaseQuery();
			EndQuery.ReleaseQuery();
			QueryCount -= 2;
		}
	}

	void Begin(FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());
		check(!bInsideQuery);
		bInsideQuery = true;

		if (StartQuery.GetQuery() != nullptr)
		{
			RHICmdList.EndRenderQuery(StartQuery.GetQuery());
			bBeginQueryInFlight = true;
		}
		StartResultMicroseconds = InvalidQueryResult;
		EndResultMicroseconds = InvalidQueryResult;
		FrameNumber = GFrameNumberRenderThread;
	}

	void End(FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());
		check(bInsideQuery);
		bInsideQuery = false;

		if ( HasQueriesAllocated() )
		{
			RHICmdList.EndRenderQuery(EndQuery.GetQuery());
			bEndQueryInFlight = true;
		}
	}

	bool GatherQueryResults(FRHICommandListImmediate& RHICmdList)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_SceneUtils_GatherQueryResults);
		// Get the query results which are still outstanding
		check(GFrameNumberRenderThread != FrameNumber);
		if ( HasQueriesAllocated() )
		{
			if (StartResultMicroseconds == InvalidQueryResult)
			{
				if (!RHICmdList.GetRenderQueryResult(StartQuery.GetQuery(), StartResultMicroseconds, false))
				{
					StartResultMicroseconds = InvalidQueryResult;
				}
				bBeginQueryInFlight = false;
			}
			if (EndResultMicroseconds == InvalidQueryResult)
			{
				if (!RHICmdList.GetRenderQueryResult(EndQuery.GetQuery(), EndResultMicroseconds, false))
				{
					EndResultMicroseconds = InvalidQueryResult;
				}
				bEndQueryInFlight = false;
			}
		}
		else
		{
			// If we don't have a query allocated, just set the results to zero
			EndResultMicroseconds = StartResultMicroseconds = 0;
		}
		return HasValidResult();
	}

	float GetResultMS() const
	{
		check(HasValidResult());
		if (EndResultMicroseconds < StartResultMicroseconds)
		{
			// This should never happen...
			return 0.0f;
		}
		return float(EndResultMicroseconds - StartResultMicroseconds) / 1000.0f;
	}

	bool HasValidResult() const
	{
		return StartResultMicroseconds != FRealtimeGPUProfilerEvent::InvalidQueryResult && EndResultMicroseconds != FRealtimeGPUProfilerEvent::InvalidQueryResult;
	}

#if STATS
	const FName& GetStatName() const
	{
		return StatName;
	}
#endif
	const FName& GetName() const
	{
		return Name;
	}

	uint64 GetStartResultMicroseconds(uint32 GPUIndex = 0) const
	{
		return StartResultMicroseconds;
	}

	uint64 GetEndResultMicroseconds(uint32 GPUIndex = 0) const
	{
		return EndResultMicroseconds;
	}

	uint32 GetFrameNumber() const
	{
		return FrameNumber;
	}

private:
	FRHIPooledRenderQuery StartQuery;
	FRHIPooledRenderQuery EndQuery;
#if STATS
	FName StatName;
#endif
	FName Name;
	uint64 StartResultMicroseconds;
	uint64 EndResultMicroseconds;
	uint32 FrameNumber;

	bool bInsideQuery;
	bool bBeginQueryInFlight;
	bool bEndQueryInFlight;
};

/*-----------------------------------------------------------------------------
FRealtimeGPUProfilerFrame class
Container for a single frame's GPU stats
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerFrame
{
	uint32& QueryCount;
public:
	FRealtimeGPUProfilerFrame(FRenderQueryPoolRHIRef InRenderQueryPool, uint32& InQueryCount)
		: QueryCount(InQueryCount)
		, FrameNumber(-1)
		, RenderQueryPool(InRenderQueryPool)
	{}

	~FRealtimeGPUProfilerFrame()
	{
		Clear(nullptr);
	}

	void PushEvent(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName)
	{
		// TODO: this should really use a pool / free list
		FRealtimeGPUProfilerEvent* Event = new FRealtimeGPUProfilerEvent(Name, StatName, RenderQueryPool, QueryCount);
		const int32 EventIndex = GpuProfilerEvents.Num();

		GpuProfilerEvents.Add(Event);

		FRealtimeGPUProfilerTimelineEvent TimelineEvent = {};
		TimelineEvent.Type = FRealtimeGPUProfilerTimelineEvent::EType::PushEvent;
		TimelineEvent.EventIndex = EventIndex;

		GpuProfilerTimelineEvents.Push(TimelineEvent);

		EventStack.Push(EventIndex);
		Event->Begin(RHICmdList);
	}

	void PopEvent(FRHICommandListImmediate& RHICmdList)
	{
		int32 Index = EventStack.Pop();

		FRealtimeGPUProfilerTimelineEvent TimelineEvent = {};
		TimelineEvent.Type = FRealtimeGPUProfilerTimelineEvent::EType::PopEvent;
		TimelineEvent.EventIndex = Index;
		GpuProfilerTimelineEvents.Push(TimelineEvent);

		GpuProfilerEvents[Index]->End(RHICmdList);
	}

	void Clear(FRHICommandListImmediate* RHICommandListPtr)
	{
		EventStack.Empty();

		for (int Index = 0; Index < GpuProfilerEvents.Num(); Index++)
		{
			if (GpuProfilerEvents[Index])
			{
				GpuProfilerEvents[Index]->ReleaseQueries(RenderQueryPool, RHICommandListPtr, QueryCount);
				delete GpuProfilerEvents[Index];
			}
		}
		GpuProfilerEvents.Empty();
		GpuProfilerTimelineEvents.Empty();
		EventAggregates.Empty();
	}

	bool UpdateStats(FRHICommandListImmediate& RHICmdList)
	{
		const bool bCsvStatsEnabled = !!CVarGPUCsvStatsEnabled.GetValueOnRenderThread();
		const bool bTracingStatsEnabled = !!CVarGPUTracingStatsEnabled.GetValueOnRenderThread();

		// Gather any remaining results and check all the results are ready
		bool bAllQueriesAllocated = true;
		bool bAnyEventFailed = false;
		for (int Index = 0; Index < GpuProfilerEvents.Num(); Index++)
		{
			FRealtimeGPUProfilerEvent* Event = GpuProfilerEvents[Index];
			check(Event != nullptr);
			if (!Event->HasValidResult())
			{
				Event->GatherQueryResults(RHICmdList);
			}
			if (!Event->HasValidResult())
			{
#if UE_BUILD_DEBUG
				UE_LOG(LogRendererCore, Warning, TEXT("Query '%s' not ready."), *Event->GetName().ToString());
#endif
				// The frame isn't ready yet. Don't update stats - we'll try again next frame. 
				bAnyEventFailed = true;
				continue;
			}
			if (!Event->HasQueriesAllocated())
			{
				bAllQueriesAllocated = false;
			}
		}

		if (bAnyEventFailed)
		{
			return false;
		}

		if (!bAllQueriesAllocated)
		{
			static bool bWarned = false;

			if (!bWarned)
			{
				bWarned = true;
				UE_LOG(LogRendererCore, Warning, TEXT("Ran out of GPU queries! Results for this frame will be incomplete"));
			}
		}

		// Calculate inclusive and exclusive time for all events

		EventAggregates.Reserve(GpuProfilerEvents.Num());
		TArray<int32, TInlineAllocator<32>> TimelineEventStack;

		for (const FRealtimeGPUProfilerEvent* Event : GpuProfilerEvents)
		{
			FGPUEventTimeAggregate Aggregate;
			Aggregate.InclusiveTime = Event->GetResultMS();
			Aggregate.ExclusiveTime = Aggregate.InclusiveTime;
			EventAggregates.Push(Aggregate);
		}

		for (const FRealtimeGPUProfilerTimelineEvent& TimelineEvent : GpuProfilerTimelineEvents)
		{
			if (TimelineEvent.Type == FRealtimeGPUProfilerTimelineEvent::EType::PushEvent)
			{
				if (TimelineEventStack.Num() != 0)
				{
					EventAggregates[TimelineEventStack.Last()].ExclusiveTime -= EventAggregates[TimelineEvent.EventIndex].InclusiveTime;
				}
				TimelineEventStack.Push(TimelineEvent.EventIndex);
			}
			else
			{
				TimelineEventStack.Pop();
			}
		}

		// Update the stats

		const bool GPUStatsChildTimesIncluded = !!CVarGPUStatsChildTimesIncluded.GetValueOnRenderThread();
		float TotalMS = 0.0f;

		TMap<FName, bool> StatSeenMap;
		for (int Index = 0; Index < GpuProfilerEvents.Num(); Index++)
		{
			FRealtimeGPUProfilerEvent* Event = GpuProfilerEvents[Index];
			check(Event != nullptr);
			check(Event->HasValidResult());
			const FName& StatName = Event->GetName();

			// Check if we've seen this stat yet 
			bool bIsNew = false;
			if (StatSeenMap.Find(StatName) == nullptr)
			{
				StatSeenMap.Add(StatName, true);
				bIsNew = true;
			}

			const float EventTime = GPUStatsChildTimesIncluded
				? EventAggregates[Index].InclusiveTime
				: EventAggregates[Index].ExclusiveTime;

#if STATS
			EStatOperation::Type StatOp = bIsNew ? EStatOperation::Set : EStatOperation::Add;
			FThreadStats::AddMessage(Event->GetStatName(), StatOp, double(EventTime));
#endif

#if CSV_PROFILER
			if (bCsvStatsEnabled)
			{
				ECsvCustomStatOp CsvStatOp = bIsNew ? ECsvCustomStatOp::Set : ECsvCustomStatOp::Accumulate;
				FCsvProfiler::Get()->RecordCustomStat(Event->GetName(), CSV_CATEGORY_INDEX(GPU), EventTime, CsvStatOp);
			}
#endif

#if TRACING_PROFILER
			if (bTracingStatsEnabled)
			{
				ANSICHAR EventName[NAME_SIZE];
				Event->GetName().GetPlainANSIString(EventName);

				const uint32 GPUIndex = 0;
				FTracingProfiler::Get()->AddGPUEvent(
					EventName,
					Event->GetStartResultMicroseconds(),
					Event->GetEndResultMicroseconds(),
					GPUIndex,
					Event->GetFrameNumber());
			}
#endif //TRACING_PROFILER

			TotalMS += EventAggregates[Index].ExclusiveTime;
		}

#if STATS
		FThreadStats::AddMessage( GET_STATFNAME(Stat_GPU_Total), EStatOperation::Set, double(TotalMS) );
#endif 

#if CSV_PROFILER
		if (bCsvStatsEnabled)
		{
			FCsvProfiler::Get()->RecordCustomStat(CSV_STAT_FNAME(Total), CSV_CATEGORY_INDEX(GPU), TotalMS, ECsvCustomStatOp::Set);
		}
#endif
		return true;
	}

private:

	TArray<FRealtimeGPUProfilerEvent*> GpuProfilerEvents;
	TArray<int32> EventStack;

	struct FRealtimeGPUProfilerTimelineEvent
	{
		enum class EType { PushEvent, PopEvent };
		EType Type;
		int32 EventIndex;
	};

	// All profiler push and pop events are recorded to calculate inclusive and exclusive timing
	// while maintaining hierarchy and not splitting events unnecessarily.
	TArray<FRealtimeGPUProfilerTimelineEvent> GpuProfilerTimelineEvents;

	struct FGPUEventTimeAggregate
	{
		float ExclusiveTime;
		float InclusiveTime;
	};
	TArray<FGPUEventTimeAggregate> EventAggregates;

	uint32 FrameNumber;
	FRenderQueryPoolRHIRef RenderQueryPool;
};

/*-----------------------------------------------------------------------------
FRealtimeGPUProfiler
-----------------------------------------------------------------------------*/
FRealtimeGPUProfiler* FRealtimeGPUProfiler::Instance = nullptr;

FRealtimeGPUProfiler* FRealtimeGPUProfiler::Get()
{
	if (Instance == nullptr)
	{
		Instance = new FRealtimeGPUProfiler;
	}
	return Instance;
}


void FRealtimeGPUProfiler::SafeRelease()
{
	if (Instance)
		Instance->Cleanup();
	Instance = nullptr;
}


FRealtimeGPUProfiler::FRealtimeGPUProfiler()
	: WriteBufferIndex(0)
	, ReadBufferIndex(1) 
	, WriteFrameNumber(-1)
	, bStatGatheringPaused(false)
	, bInBeginEndBlock(false)
{
	const int MaxGPUQueries = CVarGPUStatsMaxQueriesPerFrame.GetValueOnRenderThread();
	RenderQueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime, (MaxGPUQueries > 0) ? MaxGPUQueries * 2 : UINT32_MAX);
	for (int Index = 0; Index < NumGPUProfilerBufferedFrames; Index++)
	{
		Frames.Add(new FRealtimeGPUProfilerFrame(RenderQueryPool, QueryCount));
	}
}

void FRealtimeGPUProfiler::Release()
{
	Cleanup();
}

void FRealtimeGPUProfiler::Cleanup()
{
	for (int Index = 0; Index < Frames.Num(); Index++)
	{
		delete Frames[Index];
	}
	Frames.Empty();
	RenderQueryPool.SafeRelease();
}

void FRealtimeGPUProfiler::BeginFrame(FRHICommandListImmediate& RHICmdList)
{
	check(bInBeginEndBlock == false);
	bInBeginEndBlock = true;
}

bool AreGPUStatsEnabled()
{
	if (GSupportsTimestampRenderQueries == false || !CVarGPUStatsEnabled.GetValueOnRenderThread())
	{
		return false;
	}

#if STATS 
	return true;
#elif !CSV_PROFILER
	return false;
#else

	// If we only have CSV stats, only capture if CSV GPU stats are enabled, and we're capturing
	if (!CVarGPUCsvStatsEnabled.GetValueOnRenderThread())
	{
		return false;
	}
	if (!FCsvProfiler::Get()->IsCapturing_Renderthread())
	{
		return false;
	}

	return true;
#endif
}

void FRealtimeGPUProfiler::EndFrame(FRHICommandListImmediate& RHICmdList)
{
	// This is called at the end of the renderthread frame. Note that the RHI thread may still be processing commands for the frame at this point, however
	// The read buffer index is always 3 frames beind the write buffer index in order to prevent us reading from the frame the RHI thread is still processing. 
	// This should also ensure the GPU is done with the queries before we try to read them
	check(Frames.Num() > 0);
	check(IsInRenderingThread());
	check(bInBeginEndBlock == true);
	bInBeginEndBlock = false;
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	if (Frames[ReadBufferIndex]->UpdateStats(RHICmdList))
	{
		// On a successful read, advance the ReadBufferIndex and WriteBufferIndex and clear the frame we just read
		Frames[ReadBufferIndex]->Clear(&RHICmdList);
		WriteFrameNumber = GFrameNumberRenderThread;
		WriteBufferIndex = (WriteBufferIndex + 1) % Frames.Num();
		ReadBufferIndex = (ReadBufferIndex + 1) % Frames.Num();
		bStatGatheringPaused = false;
	}
	else
	{
		// The stats weren't ready; skip the next frame and don't advance the indices. We'll try to read the stats again next frame
		bStatGatheringPaused = true;
	}
}

void FRealtimeGPUProfiler::PushEvent(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName)
{
	check(IsInRenderingThread());
	if (bStatGatheringPaused || !bInBeginEndBlock)
	{
		return;
	}
	check(Frames.Num() > 0);
	if (WriteBufferIndex >= 0)
	{
		Frames[WriteBufferIndex]->PushEvent(RHICmdList, Name, StatName);
	}
}

void FRealtimeGPUProfiler::PopEvent(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());
	if (bStatGatheringPaused || !bInBeginEndBlock)
	{
		return;
	}
	check(Frames.Num() > 0);
	if (WriteBufferIndex >= 0)
	{
		Frames[WriteBufferIndex]->PopEvent(RHICmdList);
	}
}

/*-----------------------------------------------------------------------------
FScopedGPUStatEvent
-----------------------------------------------------------------------------*/
void FScopedGPUStatEvent::Begin(FRHICommandList& InRHICmdList, const FName& Name, const FName& StatName)
{
	check(IsInRenderingThread());
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	// Non-immediate command lists are not supported (silently fail)
	if (InRHICmdList.IsImmediate())
	{
		RHICmdList = (FRHICommandListImmediate*)&InRHICmdList;
		FRealtimeGPUProfiler::Get()->PushEvent(*RHICmdList, Name, StatName);
	}
}

void FScopedGPUStatEvent::End()
{
	check(IsInRenderingThread());
	if (!AreGPUStatsEnabled())
	{
		return;
	}
	if (RHICmdList != nullptr)
	{
		FRealtimeGPUProfiler::Get()->PopEvent(*RHICmdList);
	}
}
#endif // HAS_GPU_STATS
