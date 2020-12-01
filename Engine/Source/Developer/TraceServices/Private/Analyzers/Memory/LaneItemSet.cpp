// Copyright Epic Games, Inc. All Rights Reserved.

#include "LaneItemSet.h"
#include "Support.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
FLaneItemSet::FLaneItemSet()
{
}

////////////////////////////////////////////////////////////////////////////////
FLaneItemSet::~FLaneItemSet()
{
	Clear();
}

////////////////////////////////////////////////////////////////////////////////
int32 FLaneItemSet::Find(uint64 Address) const
{
	if (Num == 0)
	{
		return -1;
	}

	uint64 Hash = GetHash(Address);
	uint32 BucketIndex = GetBucketIndex(Hash);

	while (true)
	{
		FEntry* __restrict Cursor = Data + BucketIndex;
		for (uint32 i = BucketSize / 4;;)
		{
			uint32 Rax;
			Rax  = (Cursor[3].Address == Address)|(!Cursor[3].Address); Rax <<= 1;
			Rax |= (Cursor[2].Address == Address)|(!Cursor[2].Address); Rax <<= 1;
			Rax |= (Cursor[1].Address == Address)|(!Cursor[1].Address); Rax <<= 1;
			Rax |= (Cursor[0].Address == Address)|(!Cursor[0].Address);

			if (Rax)
			{
				Rax &= -int32(Rax);
				uint32 Index = 31 - UnsafeCountLeadingZeros(Rax);
				return Cursor[Index].Address ? int32(UPTRINT((Cursor + Index) - Data)) : -1;
			}

			Cursor += 4;

			--i;
			if (i == 0)
			{
				break;
			}
		}

		BucketIndex += BucketSize;
		if (BucketIndex >= Capacity)
		{
			BucketIndex = 0;
		}
	}

	return -1;
}

////////////////////////////////////////////////////////////////////////////////
void FLaneItemSet::Add(uint64 Address, uint32 Serial, uint32 MetadataId)
{
	if (Num >= Capacity - 1) // Leave at least one empty to prevent infinite loops
	{
		return;
	}

	return AddInternal(Address, Serial, MetadataId);
}

////////////////////////////////////////////////////////////////////////////////
void FLaneItemSet::AddInternal(uint64 Address, uint32 Serial, uint32 MetadataId)
{
	uint64 Hash = GetHash(Address);
	uint32 BucketIndex = GetBucketIndex(Hash);

	while (true)
	{
		FEntry* __restrict Cursor = Data + BucketIndex;
		for (uint32 i = BucketSize / 4;;)
		{
			uint32 Rax;
			Rax  = (Cursor[3].Address <= Tomb); Rax <<= 1;
			Rax |= (Cursor[2].Address <= Tomb); Rax <<= 1;
			Rax |= (Cursor[1].Address <= Tomb); Rax <<= 1;
			Rax |= (Cursor[0].Address <= Tomb);

			if (Rax)
			{
				Rax &= -int32(Rax);
				uint32 Index = 31 - UnsafeCountLeadingZeros(Rax);

				NumTombs -= (Cursor[Index].Address == Tomb);
				Cursor[Index] = { Address, Serial, MetadataId };

				++Num;
				return;
			}

			Cursor += 4;

			--i;
			if (i == 0)
			{
				break;
			}
		}

		BucketIndex += BucketSize;
		if (BucketIndex >= Capacity)
		{
			BucketIndex = 0;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FLaneItemSet::Update(uint32 Index, uint32 Serial, uint32 MetadataId)
{
	if (Index >= Capacity)
	{
		return false;
	}

	FEntry& Entry = Data[Index];
	Entry.Serial = Serial;
	Entry.MetadataId = MetadataId;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FLaneItemSet::Remove(uint32 Index)
{
	if (Index >= Capacity)
	{
		return;
	}

	Data[Index].Address = Tomb;
	--Num;
	++NumTombs;
}

////////////////////////////////////////////////////////////////////////////////
FLaneItemSet::FItemHandle FLaneItemSet::ReadItems() const
{
	auto Handle = FItemHandle(Data - 1);
	return NextItem(Handle);
}

////////////////////////////////////////////////////////////////////////////////
FLaneItemSet::FItemHandle FLaneItemSet::NextItem(FItemHandle Handle) const
{
	const auto *Entry = ((const FEntry*)Handle) + 1, *End = Data + Capacity;
	do
	{
		if (Entry->Address > Tomb)
		{
			return FItemHandle(Entry);
		}

		if (Entry->Address == 0)
		{
			Entry += BucketMask - (UPTRINT(Entry - Data) & BucketMask);
		}

		++Entry;
	}
	while (Entry < End);

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
int32 FLaneItemSet::GetItemIndex(FItemHandle Handle) const
{
	return PTRINT((FEntry*)Handle - Data);
}

////////////////////////////////////////////////////////////////////////////////
void FLaneItemSet::Clear()
{
	FTrackerBuffer::Free(Data);
	Num = Capacity = 0;
}

////////////////////////////////////////////////////////////////////////////////
void FLaneItemSet::Rehash(uint32 GrowSize)
{
	FEntry* __restrict PrevData = Data;
	FEntry* __restrict PrevEnd = Data + Capacity;

	NumTombs = 0;
	Capacity += GrowSize;
	SIZE_T Size = sizeof(FEntry) * Capacity;
	Data = FTrackerBuffer::AllocRaw<FEntry>(Size, EntryAlign);
	memset(Data, 0, Size);

	Num = 0;
	for (FEntry* __restrict Entry = PrevData; Entry < PrevEnd; ++Entry)
	{
		if (Entry->Address <= Tomb)
		{
			if (Entry->Address == 0)
			{
				Entry += BucketMask - (UPTRINT(Entry - PrevData) & BucketMask);
			}

			continue;
		}

		AddInternal(Entry->Address, Entry->Serial, Entry->MetadataId);
	}

	FTrackerBuffer::Free(PrevData);
}

////////////////////////////////////////////////////////////////////////////////
uint64 FLaneItemSet::GetHash(uint64 Address)
{
	static const uint64 Mix = 0x30be8efa499c249dull;
	uint64 Hash = Address >> 3;
	Hash += Address & 0xffff; Hash *= Mix; Address >>= 16;
	Hash += Address & 0xffff; Hash *= Mix;
	return Hash;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FLaneItemSet::GetBucketIndex(uint64 Hash) const
{
	return uint32((uint64(uint32(Hash)) * Capacity) >> 32ull) & ~BucketMask;
}

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
