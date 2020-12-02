// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tracker.h"
#include "CoreMinimal.h"
#include "Algo/BinarySearch.h"
#include "Lane.h"
#include "TrackerJobs.h"
#include "SbifBuilder.h"
#include "Support.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
namespace FTrackerScheduler
{
	typedef UPTRINT			FJobHandle;
	typedef UPTRINT			FWaitHandle;
	void					SubmitJob(FJobHandle Job) {}
	FJobHandle				CreateJob(const ANSICHAR* Name) { return 0; }
	void					DependsOn(FJobHandle This, FJobHandle That) {}
	void					StartAfter(FJobHandle This, FJobHandle RunsFirst) {}
	FWaitHandle				MakeWaitable(FJobHandle Job, void* Data=nullptr) { return UPTRINT(Data); }
	template <class T> T*	Wait(FWaitHandle Waitable) { return (T*)Waitable; }

	template <class T, class U>
	FJobHandle CreateJob(const ANSICHAR* Name, void (*EntryFunc)(T*), U* Data=nullptr)
	{
		EntryFunc(Data);
		return 0;
	}
};



////////////////////////////////////////////////////////////////////////////////
FTracker::FTracker(ISbifBuilder* InSbifBuilder)
: SbifBuilder(InSbifBuilder)
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
	SyncWait = 0;
	NumColumns = 1;

	uint32 EventsPerColumn = SbifBuilder->GetEventsPerColumn();
	if (EventsPerColumn == 0)
	{
		EventsPerColumn = 128 << 10;
	}
	ColumnShift = 31 - UnsafeCountLeadingZeros(EventsPerColumn);
	ColumnMask = (1 << ColumnShift) - 1;
	check(ColumnShift <= FTrackerConfig::MaxSerialBits);

	SbifBuilder->Begin(&MetadataDb);

	Provision();
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::End()
{
	Finalize();
	SbifBuilder->End();
}

////////////////////////////////////////////////////////////////////////////////
uint32 FTracker::GetCurrentSerial() const
{
	return Serial + SerialBias;
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::AddAlloc(uint64 Address, const FMetadata& Metadata)
{
#if defined(PROF_ON)
	++AllocDelta;
#endif

	uint32 MetadataId = MetadataDb.Add(
		Metadata.Owner,
		Metadata.Size,
		Metadata.Alignment,
		Metadata.bIsRealloc
	);

	FLaneInput* Input = GetLaneInput(Address);
	bool bLaneFull = Input->AddAlloc(Address, Serial, MetadataId);
	Update(bLaneFull);
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::AddFree(uint64 Address)
{
#if defined(PROF_ON)
	++FreeDelta;
#endif

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

	// Collect leaks
	FTrackerScheduler::FJobHandle SyncJob = FTrackerScheduler::CreateJob("Sync");
	FTrackerScheduler::FJobHandle RootJob = FTrackerScheduler::CreateJob("Root");

	FLeakJobData* LastJobData = nullptr;
	for (uint32 i = 0; i < FTrackerConfig::NumLanes; ++i)
	{
		const FLane* Lane = Lanes[i];
		const FLaneItemSet& ActiveSet = Lane->GetActiveSet();

		auto* JobData = FTrackerBuffer::AllocTemp<FLeakJobData>();
		JobData->Next = LastJobData;
		JobData->ActiveSet = &ActiveSet;
		JobData->SerialBias = SerialBias;
		JobData->ColumnShift = ColumnShift;
		LastJobData = JobData;

		auto LeakJob = FTrackerScheduler::CreateJob("LaneLeaks", LaneLeaksJob, JobData);
		FTrackerScheduler::StartAfter(LeakJob, RootJob);
		FTrackerScheduler::DependsOn(SyncJob, LeakJob);
	}

	FTrackerScheduler::FWaitHandle LeakWait = FTrackerScheduler::MakeWaitable(SyncJob, LastJobData);
	FTrackerScheduler::SubmitJob(RootJob);
	auto* JobData = FTrackerScheduler::Wait<FLeakJobData>(LeakWait);

	do
	{
		if (JobData->Retirees != nullptr)
		{
			ProcessRetirees(JobData);
		}

		FLeakJobData* NextData = JobData->Next;
		FTrackerBuffer::FreeTemp(JobData->Retirees);
		FTrackerBuffer::FreeTemp(JobData);
		JobData = NextData;
	}
	while (JobData != nullptr);

	// Finish off the Sbif
	/*
	uint32 MaxCell = (NumColumns << 1) - 1;
	for (uint32 i = 0, n = Sbif_GetMaxDepth(NumColumns); i <= n; ++i)
	{
		uint32 Delta = 2 << i;
		uint32 CellIndex = Sbif_GetCellAtDepth(NumColumns - 1, i);
		for (auto j : {0, 1})
		{
			if (CellIndex < MaxCell)
			{
				SbifBuilder->CloseCell(CellIndex);
			}
			CellIndex -= Delta;
		}
	}
	*/
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
	if (!SyncWait)
	{
		return nullptr;
	}

	PROF_SCOPE("Sync");

	auto* Data = FTrackerScheduler::Wait<FLaneJobData>(SyncWait);
	SyncWait = 0;
	return Data;
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::FinalizeWork(FLaneJobData* Data)
{
	// SerialBias is the start of the next batch. The batch we're are finalising
	// (e.g. Data) is up to but not including SerialBias so that's how many
	// columns we're going to need.
	while (((SerialBias + ColumnMask) >> ColumnShift) > NumColumns)
	{
		NumColumns += 1;
		SbifBuilder->AddColumn();
	}

	do
	{
		if (Data->Retirees->Num)
		{
			ProcessRetirees(Data);
		}

		FLaneJobData* NextData = Data->Next;
		FTrackerBuffer::FreeTemp(Data->Input);
		FTrackerBuffer::FreeTemp(Data->Retirees);
		FTrackerBuffer::FreeTemp(Data);
		Data = NextData;
	}
	while (Data != nullptr);
	
	/*
	for (; ClosedColumns < NumColumns - 2; ++ClosedColumns)
	{
		// Columns to close lag two behind the active column. OEIS' A091090 tells us
		// how many cells are due to close. Cell indices step by 3i, weirdly enough

		uint32 CellIndex = ClosedColumns << 1;

		uint32 C1 = ClosedColumns + 1;
		uint32 C2 = ClosedColumns + 2;
		uint32 A = 31 - UnsafeCountLeadingZeros(C1 ^ C2);
		uint32 B = (C1 & C2) != 0;

		for (uint32 i = 0, n = A + B, d = 3; i < n; ++i, d <<= 1)
		{
			SbifBuilder->CloseCell(CellIndex);
			CellIndex -= d;
		}
		
		/ *
		uint32 CellIndex = ClosedColumns << 1;
		uint32 Num = ClosedColumns + 2;
		Num = (Num & -Num); // extracts lowest set bit
		for (uint32 d = 3; ; d <<= 1) // As if by magic a multiple of three turns up!!
		{
			SbifBuilder->CloseCell(CellIndex);
			CellIndex -= d;

			Num >>= 1;
			if (Num == 0 || int32(CellIndex) < 0)
			{
				break;
			}
		}
		* /
	}
	*/
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::RetireAllocs(
	const FRetireeJobData* Data,
	uint32 Column,
	uint32 Depth,
	const FRetiree* __restrict Left,
	const FRetiree* __restrict Right)
{
	static_assert(sizeof(FRetiree) == sizeof(FSbifRetiree), "These are converted up in place so must be the same size");

	uint32 BaseColumn = Sbif_GetBaseColumn(Column, Depth);
	uint32 BaseSerial = BaseColumn << ColumnShift;

	uint32 Num = uint32(UPTRINT(Right - Left));
	for (uint32 i = 0; i < Num; ++i)
	{
		const FRetiree* __restrict In = Left + i;

		uint64 Address = In->GetAddress();
		uint32 MetadataId = In->GetMetadataId();
		uint32 StartSerial = In->GetStartSerial();
		uint32 EndSerial = In->GetEndSerial(Data->SerialBias);

		auto* __restrict Out = (FSbifRetiree* __restrict)In;
		Out->_SpaceForAddress = 0xfedbab1e;
		Out->StartSerial = StartSerial - BaseSerial;
		Out->EndSerial = EndSerial - BaseSerial;
		Out->MetadataId = MetadataId;

		check(int32(Out->StartSerial) >= 0);
		check(int32(Out->EndSerial) >= 0);
	}

	const auto* __restrict Out = (FSbifRetiree*)Left;

	FSbifContext Context;
	Context.Column = Column;
	Context.Depth = Depth;
	Context.BaseSerial = BaseSerial;
	SbifBuilder->AddRetirees(&Context, Out, Num);
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::ProcessRetirees(const FRetireeJobData* Data)
{
	PROF_SCOPE("ProcessRetirees");

	const FRetiree* __restrict Cursor = Data->Retirees->Items;
	const FRetiree* __restrict End = Cursor + Data->Retirees->Num;

	auto GetColumn = [Data] (const FRetiree* __restrict Retiree)
	{
		uint32 EndSerial = Retiree->GetEndSerial(Data->SerialBias);
		return EndSerial >> Data->ColumnShift;
	};

	auto GetDepth = [Data] (const FRetiree* __restrict Retiree)
	{
		return Sbif_GetCommonDepth(
			Retiree->GetStartSerial() >> Data->ColumnShift,
			Retiree->GetEndSerial(Data->SerialBias) >> Data->ColumnShift
		);
	};

	auto ByColumn = [&] (uint32 Column, const FRetiree& __restrict Retiree) -> uint32
	{
		return Column < GetColumn(&Retiree);
	};

	auto ByDepth = [&] (uint32 Depth, const FRetiree& __restrict Retiree) -> uint32
	{
		return Depth < GetDepth(&Retiree);
	};

	do
	{
		uint32 Depth = GetDepth(Cursor);
		TArrayView<const FRetiree> Range0(Cursor, int32(UPTRINT(End - Cursor)));
		const FRetiree* __restrict Next = Cursor + Algo::UpperBound(Range0, Depth, ByDepth);

		uint32 Column = GetColumn(Cursor);
		if (Column == GetColumn(Next - 1))
		{
			/*
			check(Sbif_GetCellAtDepth(Column, Depth) < (NumColumns * 2) - 1);
			for (; Cursor < Next; ++Cursor)
			{
				uint32 a = Cursor->GetStartSerial();
				uint32 b = Cursor->GetEndSerial(Data->SerialBias);
				uint32 bs = Cursor->GetBiasedSerial();
				check(GetColumn(Cursor) == Column);
				check(GetDepth(Cursor) == Depth);
			}
			*/
			RetireAllocs(Data, Column, Depth, Cursor, Next);
		}
		else
		{
			do
			{
				TArrayView<const FRetiree> Range1(Cursor, int32(UPTRINT(Next - Cursor)));
				const FRetiree* __restrict SubNext = Cursor + Algo::UpperBound(Range1, Column, ByColumn);

				/*
				check(Sbif_GetCellAtDepth(Column, Depth) < (NumColumns * 2) - 1);
				for (; Cursor < SubNext; ++Cursor)
				{
					uint32 a = Cursor->GetStartSerial();
					uint32 b = Cursor->GetEndSerial(Data->SerialBias);
					uint32 bs = Cursor->GetBiasedSerial();
					check(GetColumn(Cursor) == Column);
					check(GetDepth(Cursor) == Depth);
				}
				*/
				RetireAllocs(Data, Column, Depth, Cursor, SubNext);

				Cursor = SubNext;
				if (Cursor >= Next)
				{
					break;
				}
			}
			while (Column = GetColumn(Cursor), true);
		}

		Cursor = Next;
	}
	while (Cursor < End);
}

////////////////////////////////////////////////////////////////////////////////
void FTracker::Dispatch(bool bDoRehash)
{
#if defined(PROF_ON)
	Running += AllocDelta - FreeDelta;
	PROF_COUNTER("stats", {
		{ "allocs", AllocDelta },
		{ "frees", FreeDelta },
		{ "alive", Running },
	});

	PROF_COUNTER("metadata", {
		{ "#", int64(MetadataDb.GetNum()) },
		{ "collisions", int64(MetadataDb.GetCollisionNum()) },
	});

	AllocDelta = FreeDelta = 0;
#endif

	FTrackerScheduler::FJobHandle LaneJobs[FTrackerConfig::NumLanes] = {};
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

			bGrow |= !Capacity || ((100 * Num) / Capacity) > 88;				// Load % is getting too high
			bool bRehash = Num && ((100 * Tombs) / (Tombs + Num)) > 50;// Too tombstoney

			if (bGrow | bRehash)
			{
				UPTRINT JobData = UPTRINT(Lanes[i]);
				JobData ^= bGrow ? ~0ull : 0ull;
				LaneJobs[i] = FTrackerScheduler::CreateJob("LaneRehash", LaneRehashJob, (void*)JobData);
			}
		}
	}

	FTrackerScheduler::FJobHandle SyncJob = FTrackerScheduler::CreateJob("Sync");

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
		JobData->ColumnShift = ColumnShift;
		LastJobData = JobData;

		auto InputJob = FTrackerScheduler::CreateJob("LaneInput", LaneInputJob, JobData);
		auto UpdateJob = FTrackerScheduler::CreateJob("LaneUpdate", LaneUpdateJob, JobData);
		auto RetireeJob = FTrackerScheduler::CreateJob("LaneRetiree", LaneRetireeJob, JobData);

		FTrackerScheduler::StartAfter(UpdateJob, InputJob);
		FTrackerScheduler::StartAfter(RetireeJob, InputJob);

		FTrackerScheduler::DependsOn(SyncJob, UpdateJob);
		FTrackerScheduler::DependsOn(SyncJob, RetireeJob);

		if (LaneJobs[i])
		{
			FTrackerScheduler::StartAfter(InputJob, LaneJobs[i]);
		}
		else
		{
			LaneJobs[i] = InputJob;
		}
	}

	FTrackerScheduler::FJobHandle RootJob = FTrackerScheduler::CreateJob("Root");
	for (FTrackerScheduler::FJobHandle LaneJob : LaneJobs)
	{
		if (LaneJob)
		{
			FTrackerScheduler::StartAfter(LaneJob, RootJob);
		}
	}

	FLaneJobData* DoneJobData = Sync();
	SyncWait = FTrackerScheduler::MakeWaitable(SyncJob, LastJobData);
	FTrackerScheduler::SubmitJob(RootJob);
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
} // namespace Trace

/* vim: set noet : */
