// Copyright Epic Games, Inc. All Rights Reserved.

#include "TrackerJobs.h"
#include "Algo/Sort.h"
#include "Config.h"
#include "SbifIdentities.inl"
#include "Support.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
void LaneRehashJob(void* Data)
{
	bool bGrow = PTRINT(Data) < 0;
	UPTRINT Ptr = UPTRINT(Data) ^ (bGrow ? ~0ull : 0ull);
	FLane* Lane = (FLane*)Ptr;

	{
		PROF_SCOPE(bGrow ? "Grow" : "Rehash");
		Lane->GetActiveSet().Rehash(bGrow ? FTrackerConfig::ActiveSetPageSize : 0);
	}
}

////////////////////////////////////////////////////////////////////////////////
void LaneInputJob(FLaneJobData* Data)
{
	FLaneItemView Allocs = Data->Input->GetAllocs();
	FLaneItemView Frees = Data->Input->GetFrees();

	// Get everything in address order.
	{
		PROF_SCOPE("AllocSort", {{"#", Allocs.Num}});
		Algo::Sort(Allocs);
	}
	{
		PROF_SCOPE("FreeSort", {{"#", Frees.Num}});
		Algo::Sort(Frees);
	}

	auto* Retirees = FTrackerBuffer::CallocTemp<FRetirees>(Frees.Num());

	struct FLaneItemIter
	{
		FLaneItem const* __restrict	Cursor;
		FLaneItem* __restrict		Write;
		FLaneItem const* __restrict	End;
	};

	FLaneItemIter Iters[] = {
		{ Allocs.GetData(), Allocs.GetData(), Allocs.GetData() + Allocs.Num() },
		{ Frees.GetData(),  Frees.GetData(),  Frees.GetData() + Frees.Num() },
	};

	if (Allocs.Num() && Frees.Num())
	{
		PROF_SCOPE("Short allocs");

		while (true)
		{
			const FLaneItem& AllocItem = *(Iters[0].Cursor);
			const FLaneItem& FreeItem = *(Iters[1].Cursor);

			int32 Parity = (FreeItem < AllocItem);
			if (AllocItem.IsSameAddress(FreeItem) && !Parity)
			{
				uint64 Address = AllocItem.GetAddress();
				uint32 Start = AllocItem.GetSerial(Data->SerialBias);
				uint32 EndBiased = FreeItem.GetBiasedSerial();
				uint32 MetadataId = AllocItem.GetMetadataId();

				FRetiree Retiree;
				Retiree.Set(Start, EndBiased, Address, MetadataId);
				uint32 Index = Retirees->Num++;
				Retirees->Items[Index] = Retiree;

				int Eof = 0;
				Eof += ++Iters[0].Cursor >= Iters[0].End;
				Eof += ++Iters[1].Cursor >= Iters[1].End;
				if (Eof)
				{
					break;
				}
				continue;
			}

			FLaneItemIter& Iter = Iters[Parity];

			Iter.Write[0] = Iter.Cursor[0];
			Iter.Write++;

			if (++Iter.Cursor >= Iter.End)
			{
				break;
			}
		}
	}

	// Consolidate remaining allocs
	FLaneItemIter& AllocIter = Iters[0];
	if (uint32 Remaining = uint32(UPTRINT(AllocIter.End - AllocIter.Cursor)))
	{
		::memmove(AllocIter.Write, AllocIter.Cursor, sizeof(FLaneItem) * Remaining);
		AllocIter.Write += Remaining;
	}

	// Consolidate remaining frees
	FLaneItemIter& FreeIter = Iters[1];
	if (uint32 Remaining = uint32(UPTRINT(FreeIter.End - FreeIter.Cursor)))
	{
		::memmove(FreeIter.Write, FreeIter.Cursor, sizeof(FLaneItem) * Remaining);
		FreeIter.Write += Remaining;
	}

	// Resolve remaining frees.
	FLaneItem* __restrict AllocWrite = AllocIter.Write;
	FLaneItem* __restrict FreeWrite = FreeIter.Write;
	{
		uint32 FreesNum = uint32(UPTRINT(FreeWrite - Frees.GetData()));

		PROF_SCOPE("RemainingFrees");

		FLaneItemSet& ActiveSet = Data->Lane->GetActiveSet();

		for (uint32 i = 0, n = FreesNum; i < n; ++i)
		{
			FLaneItem& FreeItem = Frees[i];

			uint64 Address = FreeItem.GetAddress();
			int32 ActiveIndex = ActiveSet.Find(Address);
			if (ActiveIndex < 0)
			{
				continue;
			}

			uint32 Start = ActiveSet.GetSerial(ActiveIndex);
			uint32 EndBiased = FreeItem.GetBiasedSerial();
			uint32 MetadataId = ActiveSet.GetMetadataId(ActiveIndex);

			FRetiree Retiree;
			Retiree.Set(Start, EndBiased, Address, MetadataId);
			int32 RetireeIndex = Retirees->Num++;
			Retirees->Items[RetireeIndex] = Retiree;

			AllocWrite[0] = FreeItem;
			AllocWrite->SetActiveIndex(ActiveIndex);
			AllocWrite++;
		}
	}

	Data->Retirees = Retirees;

	uint32 AllocsNum = uint32(UPTRINT(AllocWrite - Allocs.GetData()));
	Data->SetUpdates = FLaneItemView(Allocs.GetData(), AllocsNum);
}

////////////////////////////////////////////////////////////////////////////////
void LaneUpdateJob(FLaneJobData* Data)
{
	FLaneItemView SetUpdates = Data->SetUpdates;

	if (SetUpdates.Num() == 0)
	{
		return;
	}

	{
		PROF_SCOPE("UpdateSort");

		auto Predicate = [] (const FLaneItem& Lhs, const FLaneItem& Rhs)
		{
			if (Lhs.IsSameAddress(Rhs))
			{
				return Rhs.HasMetadata();
			}

			return Lhs < Rhs;
		};
		Algo::Sort(SetUpdates, Predicate);
	}

	FLaneItemSet& ActiveSet = Data->Lane->GetActiveSet();
	const FLaneItem* __restrict Ptr = SetUpdates.GetData();

	PROF_SCOPE("SetUpdate", {
		{"update", SetUpdates.Num()},
		{"set", ActiveSet.GetNum()},
	});

	auto ApplyToSet = [&ActiveSet, Data] (const FLaneItem& Item)
	{
		if (Item.HasMetadata())
		{
			uint64 Address = Item.GetAddress();
			uint32 Serial = Item.GetSerial(Data->SerialBias);
			uint32 MetadataId = Item.GetMetadataId();
			ActiveSet.Add(Address, Serial, MetadataId);
		}
		else
		{
			uint32 Index = Item.GetActiveIndex();
			ActiveSet.Remove(Index);
		}
	};

	Data->Lane->LockWrite();
	if (SetUpdates.Num() == 1)
	{
		ApplyToSet(*Ptr);
	}
	else
	{
		const FLaneItem* __restrict Prev = Ptr;

		uint32 i = 1, n = SetUpdates.Num();
		do
		{
			const FLaneItem& Item = Ptr[i];

			if (Item.IsSameAddress(*Prev))
			{
				uint32 Index = Prev->GetActiveIndex();
				uint32 Serial = Item.GetSerial(Data->SerialBias);
				uint32 MetadataId = Item.GetMetadataId();
				ActiveSet.Update(Index, Serial, MetadataId);

				++i;
				Prev = Ptr + i;
			}
			else
			{
				ApplyToSet(*Prev);
				Prev = &Item;
			}
		}
		while (++i < n);

		if (i == n)
		{
			ApplyToSet(*Prev);
		}
	}
	Data->Lane->UnlockWrite();
}

////////////////////////////////////////////////////////////////////////////////
void LaneRetireeJob(FRetireeJobData* Data)
{
	FRetirees* Retirees = Data->Retirees;

	auto GetDepth = [Data] (const FRetiree& Retiree)
	{
		return Sbif_GetCommonDepth(
			Retiree.GetStartSerial() >> Data->ColumnShift,
			Retiree.GetEndSerial(Data->SerialBias) >> Data->ColumnShift
		);
	};

	auto Predicate = [&GetDepth] (const FRetiree& Lhs, const FRetiree& Rhs)
	{
		uint32 LhsDepth = GetDepth(Lhs);
		uint32 RhsDepth = GetDepth(Rhs);
		if (LhsDepth != RhsDepth)
		{
			return LhsDepth < RhsDepth;
		}

		return Lhs.GetBiasedSerial() < Rhs.GetBiasedSerial();
	};

	TArrayView<FRetiree> Range(Retirees->Items, Retirees->Num);
	Algo::Sort(Range, Predicate);
}

////////////////////////////////////////////////////////////////////////////////
void LaneLeaksJob(FLeakJobData* Data)
{
	const FLaneItemSet* ActiveSet = Data->ActiveSet;

	uint32 Num = ActiveSet->GetNum();

	if (Num == 0)
	{
		Data->Retirees = nullptr;
		return;
	}

	auto* Retirees = FTrackerBuffer::CallocTemp<FRetirees>(Num);
	Data->Retirees = Retirees;

	FLaneItemSet::FItemHandle SetItem = ActiveSet->ReadItems();
	for (uint32 i = 0; i < Num; ++i)
	{
		uint32 Index = ActiveSet->GetItemIndex(SetItem);

		uint64 Address = ActiveSet->GetAddress(Index);
		uint32 Start = ActiveSet->GetSerial(Index);
		uint32 MetadataId = ActiveSet->GetMetadataId(Index);

		FRetiree Retiree;
		Retiree.Set(Start, 0, Address, MetadataId);
		int32 RetireeIndex = Retirees->Num++;
		Retirees->Items[RetireeIndex] = Retiree;

		SetItem = ActiveSet->NextItem(SetItem);
	}

	PROF_SCOPE("Retirees");
	LaneRetireeJob(Data);
}

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
