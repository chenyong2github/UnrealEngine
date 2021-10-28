// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/RealtimeGPUProfiler.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProfilingDebugging/TracingProfiler.h"
#include "RenderCore.h"
#include "GPUProfiler.h"

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

void FDrawEvent::Start(FRHIComputeCommandList& InRHICmdList, FColor Color, const TCHAR* Fmt, ...)
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

void FDrawEvent::Stop()
{
	if (RHICmdList)
	{
		RHICmdList->PopEvent();
		RHICmdList = NULL;
	}
}

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
static const uint64 InvalidQueryResult = 0xFFFFFFFFFFFFFFFFull;

/*-----------------------------------------------------------------------------
FRealTimeGPUProfilerEvent class
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerEvent
{
public:
	FRealtimeGPUProfilerEvent(FRHIRenderQueryPool& RenderQueryPool)
		: StartResultMicroseconds(InvalidQueryResult)
		, EndResultMicroseconds(InvalidQueryResult)
		, StartQuery(RenderQueryPool.AllocateQuery())
		, EndQuery(RenderQueryPool.AllocateQuery())
		, FrameNumber(-1)
#if DO_CHECK
		, bInsideQuery(false)
#endif
	{
		check(StartQuery.IsValid() && EndQuery.IsValid());
	}

	void Begin(FRHICommandListImmediate& RHICmdList, const FName& NewName, const FName& NewStatName)
	{
		check(IsInRenderingThread());
		check(!bInsideQuery && StartQuery.IsValid());
#if DO_CHECK
		bInsideQuery = true;
#endif
		GPUMask = RHICmdList.GetGPUMask();
		RHICmdList.EndRenderQuery(StartQuery.GetQuery());

		Name = NewName;
		STAT(StatName = NewStatName;)
		StartResultMicroseconds = TStaticArray<uint64, MAX_NUM_GPUS>(InvalidQueryResult);
		EndResultMicroseconds = TStaticArray<uint64, MAX_NUM_GPUS>(InvalidQueryResult);
		FrameNumber = GFrameNumberRenderThread;
	}

	void End(FRHICommandListImmediate& RHICmdList)
	{
		check(IsInRenderingThread());
		check(bInsideQuery && EndQuery.IsValid());
#if DO_CHECK
		bInsideQuery = false;
#endif
		SCOPED_GPU_MASK(RHICmdList, GPUMask);
		RHICmdList.EndRenderQuery(EndQuery.GetQuery());
	}

	bool GatherQueryResults(FRHICommandListImmediate& RHICmdList)
	{
		//QUICK_SCOPE_CYCLE_COUNTER(STAT_SceneUtils_GatherQueryResults);

		// Get the query results which are still outstanding
		check(GFrameNumberRenderThread != FrameNumber);
		check(StartQuery.IsValid() && EndQuery.IsValid());

		for (uint32 GPUIndex : GPUMask)
		{
			if (StartResultMicroseconds[GPUIndex] == InvalidQueryResult)
			{
				if (!RHICmdList.GetRenderQueryResult(StartQuery.GetQuery(), StartResultMicroseconds[GPUIndex], false, GPUIndex))
				{
					StartResultMicroseconds[GPUIndex] = InvalidQueryResult;
				}
			}

			if (EndResultMicroseconds[GPUIndex] == InvalidQueryResult)
			{
				if (!RHICmdList.GetRenderQueryResult(EndQuery.GetQuery(), EndResultMicroseconds[GPUIndex], false, GPUIndex))
				{
					EndResultMicroseconds[GPUIndex] = InvalidQueryResult;
				}
			}
		}

		return HasValidResult();
	}

	uint64 GetResultUs(uint32 GPUIndex) const
	{
		check(HasValidResult(GPUIndex));

		if (StartResultMicroseconds[GPUIndex] > EndResultMicroseconds[GPUIndex])
		{
			return 0llu;
		}

		return EndResultMicroseconds[GPUIndex] - StartResultMicroseconds[GPUIndex];
	}

	bool HasValidResult(uint32 GPUIndex) const
	{
		return StartResultMicroseconds[GPUIndex] != InvalidQueryResult && EndResultMicroseconds[GPUIndex] != InvalidQueryResult;
	}

	bool HasValidResult() const
	{
		for (uint32 GPUIndex : GPUMask)
		{
			if (!HasValidResult(GPUIndex))
			{
				return false;
			}
		}
		return true;
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

	FRHIGPUMask GetGPUMask() const
	{
		return GPUMask;
	}

	uint64 GetStartResultMicroseconds(uint32 GPUIndex) const
	{
		return StartResultMicroseconds[GPUIndex];
	}

	uint64 GetEndResultMicroseconds(uint32 GPUIndex) const
	{
		return EndResultMicroseconds[GPUIndex];
	}

	uint32 GetFrameNumber() const
	{
		return FrameNumber;
	}

	static constexpr uint32 GetNumRHIQueriesPerEvent()
	{
		return 2u;
	}

	TStaticArray<uint64, MAX_NUM_GPUS> StartResultMicroseconds;
	TStaticArray<uint64, MAX_NUM_GPUS> EndResultMicroseconds;

private:
	FRHIPooledRenderQuery StartQuery;
	FRHIPooledRenderQuery EndQuery;

	FName Name;
	STAT(FName StatName;)

	FRHIGPUMask GPUMask;

	uint32 FrameNumber;

#if DO_CHECK
	bool bInsideQuery;
#endif
};

#if GPUPROFILERTRACE_ENABLED
void TraverseEventTree(
	const TArray<FRealtimeGPUProfilerEvent, TInlineAllocator<100u>>& GpuProfilerEvents,
	const TArray<TArray<int32>>& GpuProfilerEventChildrenIndices,
	int32 Root,
	uint32 GPUIndex)
{
	uint64 lastStartTime = 0;
	uint64 lastEndTime = 0;

	if (Root != 0)
	{
		check(GpuProfilerEvents[Root].GetGPUMask().Contains(GPUIndex));
		FGpuProfilerTrace::SpecifyEventByName(GpuProfilerEvents[Root].GetName());
		FGpuProfilerTrace::BeginEventByName(GpuProfilerEvents[Root].GetName(), GpuProfilerEvents[Root].GetFrameNumber(), GpuProfilerEvents[Root].GetStartResultMicroseconds(GPUIndex));
	}

	for (int32 Subroot : GpuProfilerEventChildrenIndices[Root])
	{
		// Multi-GPU support : FGpuProfilerTrace is not yet MGPU-aware.
		if (GpuProfilerEvents[Subroot].GetGPUMask().Contains(GPUIndex))
		{
			check(GpuProfilerEvents[Subroot].GetStartResultMicroseconds(GPUIndex) >= lastEndTime);
			lastStartTime = GpuProfilerEvents[Subroot].GetStartResultMicroseconds(GPUIndex);
			lastEndTime = GpuProfilerEvents[Subroot].GetEndResultMicroseconds(GPUIndex);
			check(lastStartTime <= lastEndTime);
			if (Root != 0)
			{
				check(GpuProfilerEvents[Root].GetGPUMask().Contains(GPUIndex));
				check(lastStartTime >= GpuProfilerEvents[Root].GetStartResultMicroseconds(GPUIndex));
				check(lastEndTime <= GpuProfilerEvents[Root].GetEndResultMicroseconds(GPUIndex));
			}
			TraverseEventTree(GpuProfilerEvents, GpuProfilerEventChildrenIndices, Subroot, GPUIndex);
		}
	}

	if (Root != 0)
	{
		check(GpuProfilerEvents[Root].GetGPUMask().Contains(GPUIndex));
		FGpuProfilerTrace::SpecifyEventByName(GpuProfilerEvents[Root].GetName());
		FGpuProfilerTrace::EndEvent(GpuProfilerEvents[Root].GetEndResultMicroseconds(GPUIndex));
	}
}
#endif

/*-----------------------------------------------------------------------------
FRealtimeGPUProfilerFrame class
Container for a single frame's GPU stats
-----------------------------------------------------------------------------*/
class FRealtimeGPUProfilerFrame
{
public:
	FRealtimeGPUProfilerFrame(FRenderQueryPoolRHIRef InRenderQueryPool, uint32& InQueryCount)
		: NextEventIdx(1)
		, OverflowEventCount(0)
		, NextResultPendingEventIdx(1)
		, QueryCount(InQueryCount)
		, RenderQueryPool(InRenderQueryPool)
	{
		GpuProfilerEvents.Empty(GPredictedMaxNumEvents);
		GpuProfilerEvents.AddUninitialized(GPredictedMaxNumEvents);
		FMemory::Memset(&GpuProfilerEvents[0], 0, sizeof(FRealtimeGPUProfilerEvent));

		for (uint32 Idx = 1u; Idx < GPredictedMaxNumEvents; ++Idx)
		{
			new (&GpuProfilerEvents[Idx]) FRealtimeGPUProfilerEvent(*RenderQueryPool);
		}

		QueryCount += (GPredictedMaxNumEvents - 1u) * FRealtimeGPUProfilerEvent::GetNumRHIQueriesPerEvent();

		GpuProfilerEventParentIndices.Empty(GPredictedMaxNumEvents);
		GpuProfilerEventParentIndices.AddUninitialized();

		EventStack.Empty(GPredictedMaxStackDepth);
		EventStack.Add(0);

		EventAggregates.Empty(GPredictedMaxNumEvents);
		EventAggregates.AddUninitialized();

		CPUFrameStartTimestamp = FPlatformTime::Cycles64();
	}

	~FRealtimeGPUProfilerFrame()
	{
		QueryCount -= (GpuProfilerEvents.Num() - 1) * FRealtimeGPUProfilerEvent::GetNumRHIQueriesPerEvent();
	}

	void Clear(void* Dummy)
	{
		check(!OverflowEventCount);

		NextEventIdx = 1;
		NextResultPendingEventIdx = 1;

		GpuProfilerEventParentIndices.Reset();
		GpuProfilerEventParentIndices.AddUninitialized();

		EventStack.Reset();
		EventStack.Add(0);

		EventAggregates.Reset();
		EventAggregates.AddUninitialized();
	}

	void PushEvent(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName)
	{
		if (NextEventIdx >= GpuProfilerEvents.Num())
		{
			const int32 MaxNumQueries = CVarGPUStatsMaxQueriesPerFrame.GetValueOnRenderThread();

			if (MaxNumQueries < 0 || QueryCount < (uint32)MaxNumQueries)
			{
				new (GpuProfilerEvents) FRealtimeGPUProfilerEvent(*RenderQueryPool);
				QueryCount += FRealtimeGPUProfilerEvent::GetNumRHIQueriesPerEvent();
			}
			else
			{
				++OverflowEventCount;
				return;
			}
		}

		const int32 EventIdx = NextEventIdx++;

		GpuProfilerEventParentIndices.Add(EventStack.Last());
		EventStack.Push(EventIdx);
		GpuProfilerEvents[EventIdx].Begin(RHICmdList, Name, StatName);
	}

	void PopEvent(FRHICommandListImmediate& RHICmdList)
	{
		if (OverflowEventCount)
		{
			--OverflowEventCount;
			return;
		}

		const int32 EventIdx = EventStack.Pop(false);

		GpuProfilerEvents[EventIdx].End(RHICmdList);
	}

	bool UpdateStats(FRHICommandListImmediate& RHICmdList)
	{
		// Gather any remaining results and check all the results are ready
		const int32 NumEventsThisFramePlusOne = NextEventIdx;

		for (; NextResultPendingEventIdx < NumEventsThisFramePlusOne; ++NextResultPendingEventIdx)
		{
			FRealtimeGPUProfilerEvent& Event = GpuProfilerEvents[NextResultPendingEventIdx];

			if (!Event.GatherQueryResults(RHICmdList))
			{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				UE_LOG(LogRendererCore, Warning, TEXT("Query '%s' not ready."), *Event.GetName().ToString());
#endif
				// The frame isn't ready yet. Don't update stats - we'll try again next frame. 
				return false;
			}

			FGPUEventTimeAggregate Aggregate;
			// Multi-GPU support : Tracing profiler is MGPU-aware, but not CSV profiler or Unreal stats.
			Aggregate.InclusiveTimeUs = Event.GetGPUMask().Contains(0) ? (uint32)Event.GetResultUs(0) : 0;
			Aggregate.ExclusiveTimeUs = Aggregate.InclusiveTimeUs;
			EventAggregates.Add(Aggregate);
		}

		// Calculate inclusive and exclusive time for all events
		for (int32 EventIdx = 1; EventIdx < GpuProfilerEventParentIndices.Num(); ++EventIdx)
		{
			const int32 ParentIdx = GpuProfilerEventParentIndices[EventIdx];

			EventAggregates[ParentIdx].ExclusiveTimeUs -= EventAggregates[EventIdx].InclusiveTimeUs;
		}

		// Update the stats
#if CSV_PROFILER
		const bool bCsvStatsEnabled = !!CVarGPUCsvStatsEnabled.GetValueOnRenderThread();
		FCsvProfiler* CsvProfiler = bCsvStatsEnabled ? FCsvProfiler::Get() : nullptr;
#endif
		const bool GPUStatsChildTimesIncluded = !!CVarGPUStatsChildTimesIncluded.GetValueOnRenderThread();
		uint64 TotalUs = 0llu;
		FNameSet StatSeenSet;

		for (int32 Idx = 1; Idx < NumEventsThisFramePlusOne; ++Idx)
		{
			const FRealtimeGPUProfilerEvent& Event = GpuProfilerEvents[Idx];
			const FGPUEventTimeAggregate IncExcTime = EventAggregates[Idx];

			// Multi-GPU support : Tracing profiler is MGPU-aware, but not CSV profiler or Unreal stats.
			if (Event.GetGPUMask().Contains(0))
			{
				// Check if we've seen this stat yet 
				const bool bKnownStat = StatSeenSet.Add(Event.GetName());

				const uint32 EventTimeUs = GPUStatsChildTimesIncluded ? IncExcTime.InclusiveTimeUs : IncExcTime.ExclusiveTimeUs;
				TotalUs += IncExcTime.ExclusiveTimeUs;

#if STATS
				const EStatOperation::Type StatOp = bKnownStat ? EStatOperation::Add : EStatOperation::Set;
				FThreadStats::AddMessage(Event.GetStatName(), StatOp, EventTimeUs / 1000.);
#endif

#if CSV_PROFILER
				if (CsvProfiler)
				{
					const ECsvCustomStatOp CsvStatOp = bKnownStat ? ECsvCustomStatOp::Accumulate : ECsvCustomStatOp::Set;
					CsvProfiler->RecordCustomStat(Event.GetName(), CSV_CATEGORY_INDEX(GPU), EventTimeUs / 1000.f, CsvStatOp);
				}
#endif
			}

#if TRACING_PROFILER
PRAGMA_DISABLE_DEPRECATION_WARNINGS
			const bool bTracingStatsEnabled = !!CVarGPUTracingStatsEnabled.GetValueOnRenderThread();
			if (bTracingStatsEnabled)
			{
				for (uint32 GPUIndex : Event.GetGPUMask())
				{
					FTracingProfiler::Get()->AddGPUEvent(
						Event.GetName(),
						Event.GetStartResultMicroseconds(GPUIndex),
						Event.GetEndResultMicroseconds(GPUIndex),
						GPUIndex,
						Event.GetFrameNumber());
				}
			}
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif //TRACING_PROFILER
		}

#if STATS
		FThreadStats::AddMessage(GET_STATFNAME(Stat_GPU_Total), EStatOperation::Set, TotalUs / 1000.);
#endif 

#if CSV_PROFILER
		if (CsvProfiler)
		{
			CsvProfiler->RecordCustomStat(CSV_STAT_FNAME(Total), CSV_CATEGORY_INDEX(GPU), TotalUs / 1000.f, ECsvCustomStatOp::Set);
		}
#endif

#if GPUPROFILERTRACE_ENABLED
		TArray<TArray<int32>> GpuProfilerEventChildrenIndices;
		GpuProfilerEventChildrenIndices.AddDefaulted(GpuProfilerEvents.Num());
		for (int32 EventIdx = 1; EventIdx < GpuProfilerEventParentIndices.Num(); ++EventIdx)
		{
			const int32 ParentIdx = GpuProfilerEventParentIndices[EventIdx];

			GpuProfilerEventChildrenIndices[ParentIdx].Add(EventIdx);
		}

		FGPUTimingCalibrationTimestamp Timestamps[MAX_NUM_GPUS];
		FMemory::Memzero(Timestamps);

		for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; ++GPUIndex)
		{
			FGPUTimingCalibrationTimestamp& Timestamp = Timestamps[GPUIndex];

			if (TimestampCalibrationQuery.IsValid())
			{
				Timestamp.GPUMicroseconds = TimestampCalibrationQuery->GPUMicroseconds[GPUIndex];
				Timestamp.CPUMicroseconds = TimestampCalibrationQuery->CPUMicroseconds[GPUIndex];
			}

			if (Timestamp.GPUMicroseconds == 0 || Timestamp.CPUMicroseconds == 0) // Unimplemented platforms, or invalid on the first frame
			{
				if (GpuProfilerEvents.Num() > 1)
				{
					// Align CPU and GPU frames
					Timestamp.GPUMicroseconds = GpuProfilerEvents[1].GetStartResultMicroseconds(GPUIndex);
					Timestamp.CPUMicroseconds = FPlatformTime::ToSeconds64(CPUFrameStartTimestamp) * 1000 * 1000;
				}
				else
				{
					// Fallback to legacy
					Timestamp = FGPUTiming::GetCalibrationTimestamp();
				}
			}
		}		

		// Sanitize event start/end times
		TArray<TStaticArray<uint64, MAX_NUM_GPUS>> lastEndTimes;
		lastEndTimes.AddZeroed(GpuProfilerEvents.Num());
		for (int32 EventIdx = 1; EventIdx < GpuProfilerEventParentIndices.Num(); ++EventIdx)
		{
			const int32 ParentIdx = GpuProfilerEventParentIndices[EventIdx];
			FRealtimeGPUProfilerEvent& Event = GpuProfilerEvents[EventIdx];
			
			for (uint32 GPUIndex : Event.GetGPUMask())
			{
				// Start time must be >= last end time
				Event.StartResultMicroseconds[GPUIndex] = FMath::Max(Event.StartResultMicroseconds[GPUIndex], lastEndTimes[ParentIdx][GPUIndex]);
				// End time must be >= start time
				Event.EndResultMicroseconds[GPUIndex] = FMath::Max(Event.StartResultMicroseconds[GPUIndex], Event.EndResultMicroseconds[GPUIndex]);

				if (ParentIdx != 0)
				{
					FRealtimeGPUProfilerEvent& EventParent = GpuProfilerEvents[ParentIdx];

					// Clamp start/end times to be inside parent start/end times
					Event.StartResultMicroseconds[GPUIndex] = FMath::Clamp(Event.StartResultMicroseconds[GPUIndex],
						EventParent.StartResultMicroseconds[GPUIndex],
						EventParent.EndResultMicroseconds[GPUIndex]);
					Event.EndResultMicroseconds[GPUIndex] = FMath::Clamp(Event.EndResultMicroseconds[GPUIndex],
						Event.StartResultMicroseconds[GPUIndex],
						EventParent.EndResultMicroseconds[GPUIndex]);
				}

				// Update last end time for this parent
				lastEndTimes[ParentIdx][GPUIndex] = Event.EndResultMicroseconds[GPUIndex];
			}
		}

		for (uint32 GPUIndex = 0; GPUIndex < GNumExplicitGPUsForRendering; ++GPUIndex)
		{
			FGpuProfilerTrace::BeginFrame(Timestamps[GPUIndex]);
			TraverseEventTree(GpuProfilerEvents, GpuProfilerEventChildrenIndices, 0, GPUIndex);
			FGpuProfilerTrace::EndFrame(GPUIndex);
		}
#endif

		return true;
	}

	uint64 CPUFrameStartTimestamp;
	FTimestampCalibrationQueryRHIRef TimestampCalibrationQuery;

private:
	struct FGPUEventTimeAggregate
	{
		uint32 ExclusiveTimeUs;
		uint32 InclusiveTimeUs;
	};

	static constexpr uint32 GPredictedMaxNumEvents = 100u;
	static constexpr uint32 GPredictedMaxNumEventsUpPow2 = 128u;
	static constexpr uint32 GPredictedMaxStackDepth = 32u;

	class FNameSet
	{
	public:
		FNameSet()
			: NumElements(0)
			, Capacity(GInitialCapacity)
			, SecondaryStore(nullptr)
		{
			FMemory::Memset(InlineStore, 0, GInitialCapacity * sizeof(FName));
		}

		~FNameSet()
		{
			if (SecondaryStore)
			{
				FMemory::Free(SecondaryStore);
				SecondaryStore = nullptr;
			}
		}

		// @return Whether Name is already in set
		bool Add(const FName& Name)
		{
			check(Name != NAME_None);

			if (NumElements * GResizeDivFactor > Capacity)
			{
				uint32 NewCapacity = Capacity;

				do
				{
					NewCapacity *= 2u;
				} while (NumElements * GResizeDivFactor > NewCapacity);

				Resize(NewCapacity);
			}

			FName* NameStore = GetNameStore();
			const uint32 NameHash = GetTypeHash(Name);
			const uint32 Mask = Capacity - 1;
			uint32 Idx = NameHash & Mask;
			uint32 Probe = 1;
			const FName NameNone = NAME_None;

			while (NameNone != NameStore[Idx] && Name != NameStore[Idx])
			{
				Idx = (Idx + Probe) & Mask;
				Probe++;
			}

			if (NameNone != NameStore[Idx])
			{
				return true;
			}
			else
			{
				NameStore[Idx] = Name;
				++NumElements;
				return false;
			}
		}

	private:
		void Resize(uint32 NewCapacity)
		{
			const bool bNeedFree = !!SecondaryStore;
			FName* OldStore = bNeedFree ? SecondaryStore : InlineStore;

			SecondaryStore = (FName*)FMemory::Malloc(NewCapacity * sizeof(FName));
			FMemory::Memset(SecondaryStore, 0, NewCapacity * sizeof(FName));

			const uint32 OldCapacity = Capacity;
			Capacity = NewCapacity;
			NumElements = 0;

			for (uint32 Idx = 0; Idx < OldCapacity; ++Idx)
			{
				const FName& Name = OldStore[Idx];
				if (Name != NAME_None)
				{
					Add(Name);
				}
			}

			if (bNeedFree)
			{
				FMemory::Free(OldStore);
			}
		}

		FName* GetNameStore()
		{
			return SecondaryStore ? SecondaryStore : InlineStore;
		}

		static constexpr uint32 GResizeDivFactor = 2u;
		static constexpr uint32 GInitialCapacity = GPredictedMaxNumEventsUpPow2 * GResizeDivFactor;

		uint32 NumElements;
		uint32 Capacity;
		FName InlineStore[GInitialCapacity];
		FName* SecondaryStore;
	};

	int32 NextEventIdx;
	int32 OverflowEventCount;
	int32 NextResultPendingEventIdx;

	uint32& QueryCount;
	FRenderQueryPoolRHIRef RenderQueryPool;

	TArray<FRealtimeGPUProfilerEvent, TInlineAllocator<GPredictedMaxNumEvents>> GpuProfilerEvents;
	TArray<int32, TInlineAllocator<GPredictedMaxNumEvents>> GpuProfilerEventParentIndices;
	TArray<int32, TInlineAllocator<GPredictedMaxStackDepth>> EventStack;
	TArray<FGPUEventTimeAggregate, TInlineAllocator<GPredictedMaxNumEvents>> EventAggregates;
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
	if (GSupportsTimestampRenderQueries)
	{
		const int MaxGPUQueries = CVarGPUStatsMaxQueriesPerFrame.GetValueOnRenderThread();
		RenderQueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime, (MaxGPUQueries > 0) ? MaxGPUQueries * 2 : UINT32_MAX);
		for (int Index = 0; Index < NumGPUProfilerBufferedFrames; Index++)
		{
			Frames.Add(new FRealtimeGPUProfilerFrame(RenderQueryPool, QueryCount));
		}
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

#if UE_TRACE_ENABLED
namespace GpuProfilerTrace
{
	RHI_API UE_TRACE_CHANNEL_EXTERN(GpuChannel)
}
#endif

void FRealtimeGPUProfiler::BeginFrame(FRHICommandListImmediate& RHICmdList)
{
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	check(bInBeginEndBlock == false);
	bInBeginEndBlock = true;

	Frames[WriteBufferIndex]->TimestampCalibrationQuery = new FRHITimestampCalibrationQuery();
	RHICmdList.CalibrateTimers(Frames[WriteBufferIndex]->TimestampCalibrationQuery);
	Frames[WriteBufferIndex]->CPUFrameStartTimestamp = FPlatformTime::Cycles64();
}

bool AreGPUStatsEnabled()
{
	if (GSupportsTimestampRenderQueries == false || !CVarGPUStatsEnabled.GetValueOnRenderThread())
	{
		return false;
	}

#if GPUPROFILERTRACE_ENABLED
	// Force GPU profiler on if Unreal Insights is running
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(GpuProfilerTrace::GpuChannel))
	{
		return true;
	}
#endif

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
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	// This is called at the end of the renderthread frame. Note that the RHI thread may still be processing commands for the frame at this point, however
	// The read buffer index is always 3 frames beind the write buffer index in order to prevent us reading from the frame the RHI thread is still processing. 
	// This should also ensure the GPU is done with the queries before we try to read them
	check(Frames.Num() > 0);
	check(IsInRenderingThread());
	check(bInBeginEndBlock == true);
	bInBeginEndBlock = false;

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

void FRealtimeGPUProfiler::PushStat(FRHICommandListImmediate& RHICmdList, const FName& Name, const FName& StatName, int32 (*InNumDrawCallsPtr)[MAX_NUM_GPUS])
{
	PushEvent(RHICmdList, Name, StatName);

	if (InNumDrawCallsPtr && (**InNumDrawCallsPtr) != -1)
	{
		RHICmdList.EnqueueLambda([InNumDrawCallsPtr](FRHICommandListImmediate&)
		{
			GCurrentNumDrawCallsRHIPtr = InNumDrawCallsPtr;
		});
	}
}

void FRealtimeGPUProfiler::PopStat(FRHICommandListImmediate& RHICmdList, int32 (*InNumDrawCallsPtr)[MAX_NUM_GPUS])
{
	PopEvent(RHICmdList);

	if (InNumDrawCallsPtr && (**InNumDrawCallsPtr) != -1)
	{
		RHICmdList.EnqueueLambda([](FRHICommandListImmediate&)
		{
			GCurrentNumDrawCallsRHIPtr = &GCurrentNumDrawCallsRHI;
		});
	}
}

/*-----------------------------------------------------------------------------
FScopedGPUStatEvent
-----------------------------------------------------------------------------*/
void FScopedGPUStatEvent::Begin(FRHICommandList& InRHICmdList, const FName& Name, const FName& StatName, int32 (*InNumDrawCallsPtr)[MAX_NUM_GPUS])
{
	check(IsInRenderingThread());
	if (!AreGPUStatsEnabled())
	{
		return;
	}

	// Non-immediate command lists are not supported (silently fail)
	if (InRHICmdList.IsImmediate())
	{
		NumDrawCallsPtr = InNumDrawCallsPtr;
		RHICmdList = &static_cast<FRHICommandListImmediate&>(InRHICmdList);
		FRealtimeGPUProfiler::Get()->PushStat(*RHICmdList, Name, StatName, InNumDrawCallsPtr);
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
		FRealtimeGPUProfiler::Get()->PopStat(*RHICmdList, NumDrawCallsPtr);
	}
}
#endif // HAS_GPU_STATS
