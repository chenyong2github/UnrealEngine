// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetadataDb.h"
#include "Support.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
bool FMetadataDb::FEntryInternal::operator == (const FEntryInternal& Rhs) const
{
	return (Owner == Rhs.Owner) | (Size == Rhs.Size);
}

////////////////////////////////////////////////////////////////////////////////
FMetadataDb::FMetadataDb()
{
}

////////////////////////////////////////////////////////////////////////////////
FMetadataDb::~FMetadataDb()
{
}

////////////////////////////////////////////////////////////////////////////////
uint32 FMetadataDb::GetNum() const
{
	return uint32(Map.Num());
}

////////////////////////////////////////////////////////////////////////////////
uint32 FMetadataDb::GetCollisionNum() const
{
	return Collisions;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FMetadataDb::Add(uint64 Owner, uint64 Size, uint32 Alignment, bool bRealloc)
{
	static const uint64 Mix = 0x00000100000001b3;

	uint64 Id = 0xcbf29ce484222325;
	Id ^= Owner;	Id *= Mix;
	Id ^= Size;		Id *= Mix;
	Id ^= Alignment;Id *= Mix;

	uint32 SizeTribble = uint32(Size & 0x07);
	uint32 AlignmentPow2 = uint8(31 - UnsafeCountLeadingZeros(Alignment));

	FEntryInternal Entry;
	Entry.Id = 0;
	Entry.Size = uint32(Size >> 3);
	Entry.Owner = Owner;
	Entry.Alignment_SizeTribble = uint8((AlignmentPow2 << 3) | SizeTribble);
	Entry.Alignment_SizeTribble |= (bRealloc == true);

	return AddInternal(Id, Entry);
}

////////////////////////////////////////////////////////////////////////////////
uint32 FMetadataDb::AddInternal(uint64 Id, const FEntryInternal& Entry)
{
	/* To make this thread safe, only do the look-up in this function. If the
	 * entry needs adding queue the add up to one side. Flush the queue when it
	 * is guaranteed there is no other reading going on. Removes need to lock */

	uint32 SmallerId = uint32(Id);

	FEntryInternal& Out = Map.FindOrAdd(SmallerId);
	if (LIKELY(Out == Entry))
	{
		return Id;
	}

	if (Out.Id == 0)
	{
		Out = Entry;
		Out.Id = SmallerId;
		return SmallerId;
	}

	++Collisions;
	Id = Remix(Id);
	return AddInternal(Id, Entry); // tail call opt. one hopes...
}

////////////////////////////////////////////////////////////////////////////////
const FMetadataDb::FEntry* FMetadataDb::Get(uint32 Id) const
{
	const FEntryInternal* Iter = Map.Find(Id);
	return (const FEntry*)Iter;
}

////////////////////////////////////////////////////////////////////////////////
uint64 FMetadataDb::Remix(uint64 Id)
{
	return Id * 0x30be8efa499c249dull;
}

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
