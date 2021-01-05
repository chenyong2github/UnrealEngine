// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracker.h"
#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "Lane.h"
#include "TrackerJobs.h"
#include "RetireeSink.h"
#include "Support.h"

namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
namespace FTrackerScheduler
{

////////////////////////////////////////////////////////////////////////////////
template <typename T, typename U>
struct FTrackerGraphTask
{
	typedef void (EntryFuncType)(T*);

	FTrackerGraphTask(EntryFuncType* InEntryFunc, U* InData)
	: EntryFunc(InEntryFunc)
	, Data(InData)
	{
	}

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		EntryFunc(Data);
	}

	EntryFuncType*	EntryFunc;
	U*				Data;

	ENamedThreads::Type GetDesiredThread()				{ return ENamedThreads::AnyThread; }
	TStatId GetStatId() const							{ return TStatId(); }
	static ESubsequentsMode::Type GetSubsequentsMode()	{ return ESubsequentsMode::TrackSubsequents; }
};

////////////////////////////////////////////////////////////////////////////////
static FGraphEventRef CreateJob(const ANSICHAR* Name, const FGraphEventArray& Prereqs)
{
	return TGraphTask<FNullGraphTask>::CreateTask(&Prereqs)
		.ConstructAndDispatchWhenReady(TStatId(), ENamedThreads::AnyThread);
}

////////////////////////////////////////////////////////////////////////////////
template <typename T, typename U>
static FGraphEventRef CreateJob(
	const ANSICHAR*	Name,
	void			(*EntryFunc)(T*),
	U*				Data=nullptr,
	FGraphEventRef	Prereq=0)
{
	FGraphEventArray Prereqs;
	if (Prereq)
	{
		Prereqs.Add(Prereq);
	}

	return TGraphTask<FTrackerGraphTask<T,U>>::CreateTask(&Prereqs)
		.ConstructAndDispatchWhenReady(EntryFunc, Data);
}

////////////////////////////////////////////////////////////////////////////////
static void Wait(FGraphEventRef Job)
{
	return FTaskGraphInterface::Get().WaitUntilTaskCompletes(Job);
}

};



////////////////////////////////////////////////////////////////////////////////
FTracker::FTracker(IRetireeSink* InRetireeSink)
: RetireeSink(InRetireeSink)
{
	for (FLane*& Lane : Lanes)
	{
		Lane = new FLane();
	}
}

////////////////////////////////////////////////////////////////////////////////
FTracker::~FTracker()
{
	for (FLane* Lane : Lanes)
	{
		delete Lane;
	}
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::Begin()
{
	SerialBias = 0;
	Serial = 0;
	SyncWait = {};

	Provision();
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::End()
{
	Finalize();
}

////////////////////////////////////////////////////////////////////////////////
uint32 FTracker::GetCurrentSerial() const
{
	return Serial + SerialBias;
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::AddAlloc(uint64 Address, uint32 MetadataId)
{
	FLaneInput* Input = GetLaneInput(Address);
	bool bLaneFull = Input->AddAlloc(Address, Serial, MetadataId);
	Update(bLaneFull);
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::AddFree(uint64 Address)
{
	FLaneInput* Input = GetLaneInput(Address);
	bool bLaneFull = Input->AddFree(Address, Serial);
	Update(bLaneFull);
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::Update(bool bDispatch)
{
	++Serial;
	
	if (!bDispatch)
	{
		return;
	}

	Dispatch();
	Provision();
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::Finalize()
{
	// Flush lanes. Provision() usually tweaks SerialBias but we don't want to
	// provision so we'll do it here ourselves.
	Dispatch(false);

	SerialBias += Serial;
	Serial = 0;

	if (FLaneJobData* DoneJobData = Sync())
	{
		FinalizeWork(DoneJobData);
	}

	// Reuse the last serial submitted to the Sbif for leaks. Saves dealing with
	// column overflow by one serial
	SerialBias -= 1;

	FGraphEventArray TailJobs;
	TailJobs.Reserve(FTrackerConfig::NumLanes);

	// Dispatch jobs to collect leaks on each lane
	FLeakJobData* LastJobData = nullptr;
	for (uint32 i = 0; i < FTrackerConfig::NumLanes; ++i)
	{
		const FLane* Lane = Lanes[i];
		const FLaneItemSet& ActiveSet = Lane->GetActiveSet();

		auto* JobData = FTrackerBuffer::AllocTemp<FLeakJobData>();
		JobData->Next = LastJobData;
		JobData->ActiveSet = &ActiveSet;
		JobData->SerialBias = SerialBias;
		LastJobData = JobData;

		auto LeakJob = FTrackerScheduler::CreateJob("LaneLeaks", LaneLeaksJob, JobData);
		TailJobs.Add(LeakJob);
	}

	auto SyncJob = FTrackerScheduler::CreateJob("Sync", TailJobs);
	FTrackerScheduler::Wait(SyncJob);

	ProcessRetirees(LastJobData);

	do
	{
		auto* NextData = (FLeakJobData*)(LastJobData->Next);
		FTrackerBuffer::FreeTemp(LastJobData->Retirees);
		FTrackerBuffer::FreeTemp(LastJobData);
		LastJobData = NextData;
	}
	while (LastJobData != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::Provision()
{
	for (FLaneInput*& LaneInput : LaneInputs)
	{
		LaneInput = FTrackerBuffer::CallocTemp<FLaneInput>(FTrackerConfig::MaxLaneInputItems);
	}

	SerialBias += Serial;
	Serial = 0;
}

////////////////////////////////////////////////////////////////////////////////
FLaneJobData* FTracker::Sync()
{
	if (!SyncWait.JobRef.IsValid())
	{
		return nullptr;
	}

	FTrackerScheduler::Wait(SyncWait.JobRef);
	auto* Data = (FLaneJobData*)(SyncWait.WaitParam);
	SyncWait = {};
	return Data;
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::FinalizeWork(FLaneJobData* Data)
{

	ProcessRetirees(Data);

	do
	{
		auto* NextData = (FLaneJobData*)(Data->Next);
		FTrackerBuffer::FreeTemp(Data->Input);
		FTrackerBuffer::FreeTemp(Data->Retirees);
		FTrackerBuffer::FreeTemp(Data);
		Data = NextData;
	}
	while (Data != nullptr);
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::ProcessRetirees(const FRetireeJobData* Data)
{
	auto Comparison = [] (const FRetiree& Lhs, const FRetiree& Rhs)
	{
		return Lhs.GetSortKey() < Rhs.GetSortKey();
	};

	IRetireeSink::FRetirements SinkParam;
	SinkParam.SerialBias = Data->SerialBias;

	// Build a min-heap of each retiree list
	uint32 NumRetirees = 0;
	TArray<const FRetiree*> Heap;
	do
	{
		if (Data->Retirees != nullptr && Data->Retirees->Num)
		{
			NumRetirees += Data->Retirees->Num;
			Heap.Add(Data->Retirees->Items);
		}
		Data = (FRetireeJobData*)(Data->Next);
	}
	while (Data != nullptr);
	Heap.Heapify(Comparison);

	static const int32 BundleSize = 2048;
	TArray<FRetiree> Bundle;
	Bundle.Reserve(BundleSize);

	for (; NumRetirees; --NumRetirees)
	{
		const FRetiree* Ret = Heap.HeapTop();

		// Retiree lists are null terminated
		const FRetiree* Next = Ret + 1;
		if (Next->GetSortKey())
		{
			Heap.Add(Next);
		}
		Heap.HeapPopDiscard(Comparison);

		Bundle.Add(*Ret);
		if (Bundle.Num() >= BundleSize)
		{
			SinkParam.Retirees = Bundle;
			RetireeSink->RetireAllocs(SinkParam);
			Bundle.SetNum(0);
		}
	}

	if (Bundle.Num() > 0)
	{
		SinkParam.Retirees = Bundle;
		RetireeSink->RetireAllocs(SinkParam);
		Bundle.SetNum(0);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::Dispatch(bool bDoRehash)
{
	FLaneJobData* DoneJobData = Sync();

	FGraphEventRef LaneJobs[FTrackerConfig::NumLanes] = {};
	if (bDoRehash)
	{
		for (uint32 i = 0; i < FTrackerConfig::NumLanes; ++i)
		{
			FLaneItemSet& ActiveSet = Lanes[i]->GetActiveSet();
			uint32 Capacity = ActiveSet.GetCapacity();
			uint32 Num = ActiveSet.GetNum();
			uint32 Tombs = ActiveSet.GetNumTombs();

			/* The below needs more thought */

			uint32 AllocNum = LaneInputs[i]->GetAllocs().Num();

			bool bGrow = (Capacity - Num) <= AllocNum; // Not enough space to fit potential allocs

			bGrow |= !Capacity || ((100 * Num) / Capacity) > 88;		// Load % is getting too high
			bool bRehash = Num && ((100 * Tombs) / (Tombs + Num)) > 50;	// Too tombstoney

			if (bGrow | bRehash)
			{
				UPTRINT JobData = UPTRINT(Lanes[i]);
				JobData ^= bGrow ? ~0ull : 0ull;
				LaneJobs[i] = FTrackerScheduler::CreateJob("LaneRehash", LaneRehashJob, (void*)JobData);
			}
		}
	}

	FGraphEventArray TailJobs;
	TailJobs.Reserve(FTrackerConfig::NumLanes * 2);

	// Lane input jobs
	FLaneJobData* LastJobData = nullptr;
	for (uint32 i = 0; i < FTrackerConfig::NumLanes; ++i)
	{
		FLaneInput* LaneInput = LaneInputs[i];
		if (LaneInput->GetNum() == 0)
		{
			continue;
		}

		auto* JobData = FTrackerBuffer::AllocTemp<FLaneJobData>();
		JobData->Next = LastJobData;
		JobData->Lane = Lanes[i];
		JobData->Input = LaneInput;
		JobData->SerialBias = SerialBias;
		LastJobData = JobData;

		auto InputJob = FTrackerScheduler::CreateJob("LaneInput", LaneInputJob, JobData, LaneJobs[i]);
		auto UpdateJob = FTrackerScheduler::CreateJob("LaneUpdate", LaneUpdateJob, JobData, InputJob);
		auto RetireeJob = FTrackerScheduler::CreateJob("LaneRetiree", LaneRetireeJob, JobData, InputJob);

		TailJobs.Add(UpdateJob);
		TailJobs.Add(RetireeJob);
	}

	auto SyncJob = FTrackerScheduler::CreateJob("Sync", TailJobs);

	SyncWait = { SyncJob, LastJobData };
	if (DoneJobData != nullptr)
	{
		FinalizeWork(DoneJobData);
	}
}

////////////////////////////////////////////////////////////////////////////////
FLaneInput* FTracker::GetLaneInput(uint64 Address)
{
	uint32 SmallKey = uint32(Address >> 4);
	uint32 Index = ((SmallKey * 0x397666d) >> 8) & (FTrackerConfig::NumLanes - 1);
	return LaneInputs[Index];
}

} // namespace TraceServices
