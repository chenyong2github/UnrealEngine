// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetadataDb.h"
#include "Support.h"

namespace TraceServices {

// Used to unsure opaque Id return to outside world won't be zero //////////////
enum { MetadataDbBias = 1 };

////////////////////////////////////////////////////////////////////////////////
bool FMetadataDb::FEntryInternal::operator == (const FEntryInternal& Rhs) const
{
	return FMemory::Memcmp(this, &Rhs, sizeof(*this)) == 0;
}

////////////////////////////////////////////////////////////////////////////////
FMetadataDb::FMetadataDb()
{
	Map.Reserve(2 << 20);
	Entries.Reserve(2 << 20);
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
uint32 FMetadataDb::Add(uint64 Owner, uint64 Size, uint32 Alignment, uint32 Tag, bool bRealloc)
{
	static const uint64 Mix = 0x00000100000001b3;

	uint64 Id = 0xcbf29ce484222325;
	Id ^= Owner;		Id *= Mix;
	Id ^= Size;			Id *= Mix;
	Id ^= Alignment;	Id *= Mix;
	Id ^= Tag;			Id *= Mix;
	Id ^= !!bRealloc;	Id *= Mix;

	FEntryInternal Entry = {};
	Entry.Size = Size;
	Entry.Owner = Owner;
	Entry.Alignment = uint16(Alignment);
	Entry.bIsRealloc = !!bRealloc;

	return AddInternal(Id, Entry);
}

////////////////////////////////////////////////////////////////////////////////
uint32 FMetadataDb::AddInternal(uint64 Id, const FEntryInternal& Entry)
{
	/* To make this thread safe, only do the look-up in this function. If the
	 * entry needs adding queue the add up to one side. Flush the queue when it
	 * is guaranteed there is no other reading going on. Removes need to lock */

	uint32 SmallerId = uint32(Id);

	uint32& Index = Map.FindOrAdd(SmallerId, ~0u);
	if (UNLIKELY(Index == ~0u))
	{
		Index = Entries.Num();
		Entries.Add(Entry);
		return Index + MetadataDbBias;
	}

	if (LIKELY(Entries[Index] == Entry))
	{
		return Index + MetadataDbBias;
	}

	++Collisions;
	Id = Remix(Id);
	return AddInternal(Id, Entry); // tail call opt. one hopes...
}

////////////////////////////////////////////////////////////////////////////////
const FMetadataDb::FEntry* FMetadataDb::Get(uint32 Id) const
{
	Id -= MetadataDbBias;
	return (const FEntry*)((Id < uint32(Entries.Num())) ? &(Entries[Id]) : nullptr);
}

////////////////////////////////////////////////////////////////////////////////
uint64 FMetadataDb::Remix(uint64 Id)
{
	return Id * 0x30be8efa499c249dull;
}

} // namespace TraceServices
