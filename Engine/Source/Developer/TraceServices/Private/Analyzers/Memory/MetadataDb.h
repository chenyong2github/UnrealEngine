// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"

namespace Trace {
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
	uint32				Add(uint64 Owner, uint64 Size, uint32 Alignment, bool bRealloc);
	const FEntry*		Get(uint32 Id) const;

private:
	struct FEntryInternal
	{
		bool			operator == (const FEntryInternal& Rhs) const;
		uint32			Id;
		uint32			Size;
		union
		{
			uint64		Owner;
			struct
			{
				uint8	_Padding[7];
				uint8	Alignment_SizeTribble; // alignment = pow2
			};
		};
	};

	typedef TMap<uint32, FEntryInternal> MetadataMap;

	static uint64		Remix(uint64 Id);
	uint32				AddInternal(uint64 Id, const FEntryInternal& Entry);
	uint32				Collisions = 0;
	MetadataMap			Map;
};

////////////////////////////////////////////////////////////////////////////////
inline uint64 FMetadataDb::FEntry::GetOwner() const
{
	const auto* Internal = (FEntryInternal*)this;
	return Internal->Owner & 0x00ff'ffff'ffff'ffff;
}

////////////////////////////////////////////////////////////////////////////////
inline uint64 FMetadataDb::FEntry::GetSize() const
{
	const auto* Internal = (FEntryInternal*)this;
	return (uint64(Internal->Size) << 3) + (Internal->Alignment_SizeTribble & 0x07);
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FMetadataDb::FEntry::GetAlignment() const
{
	const auto* Internal = (FEntryInternal*)this;
	return 1 << (Internal->Alignment_SizeTribble >> 3) & ~1;
}

////////////////////////////////////////////////////////////////////////////////
inline bool FMetadataDb::FEntry::IsRealloc() const
{
	const auto* Internal = (FEntryInternal*)this;
	return (Internal->Alignment_SizeTribble & (1 << 3)) != 0;
}

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
