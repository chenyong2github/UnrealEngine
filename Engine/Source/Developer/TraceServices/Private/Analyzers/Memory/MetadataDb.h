// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"

namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
class FMetadataDb
{
public:
	struct FEntry
	{
		uint64			GetOwner() const;
		uint64			GetSize() const;
		uint32			GetAlignment() const;
		bool			IsRealloc() const;
	};

						FMetadataDb();
						~FMetadataDb();
	uint32				GetNum() const;
	uint32				GetCollisionNum() const;
	uint32				Add(uint64 Owner, uint64 Size, uint32 Alignment, uint32 Tag, bool bRealloc);
	const FEntry*		Get(uint32 Id) const;

private:
	struct FEntryInternal
	{
		bool			operator == (const FEntryInternal& Rhs) const;
		uint64			Owner;
		uint64			Size;
		uint32			Tag;
		uint16			Alignment;
		uint8			bIsRealloc	: 1;
		uint8			_Padding0	: 7;
		uint8			_Padding1;
	};

	using MetadataMap	= TMap<uint32, uint32>;
	using MetadataArray	= TArray<FEntryInternal>;

	static uint64		Remix(uint64 Id);
	uint32				AddInternal(uint64 Id, const FEntryInternal& Entry);
	uint32				Collisions = 0;
	MetadataMap			Map;
	MetadataArray		Entries;
};

////////////////////////////////////////////////////////////////////////////////
inline uint64 FMetadataDb::FEntry::GetOwner() const
{
	const auto* Internal = (FEntryInternal*)this;
	return Internal->Owner;
}

////////////////////////////////////////////////////////////////////////////////
inline uint64 FMetadataDb::FEntry::GetSize() const
{
	const auto* Internal = (FEntryInternal*)this;
	return Internal->Size;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FMetadataDb::FEntry::GetAlignment() const
{
	const auto* Internal = (FEntryInternal*)this;
	return Internal->Alignment;
}

////////////////////////////////////////////////////////////////////////////////
inline bool FMetadataDb::FEntry::IsRealloc() const
{
	const auto* Internal = (FEntryInternal*)this;
	return (Internal->bIsRealloc != 0);
}

} // namespace TraceServices
