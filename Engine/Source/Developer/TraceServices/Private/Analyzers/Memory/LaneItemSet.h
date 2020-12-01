// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
class FLaneItemSet
{
public:
	typedef UPTRINT	FItemHandle;
					FLaneItemSet();
					~FLaneItemSet();
	void			Clear();
	void			Rehash(uint32 GrowSize);
	uint32			GetNum() const	{ return Num; }
	uint32			GetCapacity() const { return Capacity; }
	uint32			GetNumTombs() const { return NumTombs; }
	int32			Find(uint64 Address) const;
	uint64			GetAddress(uint32 Index) const;
	uint32			GetSerial(uint32 Index) const;
	uint32			GetMetadataId(uint32 Index) const;
	void			Add(uint64 Address, uint32 Serial, uint32 MetadataId);
	bool			Update(uint32 Index, uint32 Serial, uint32 MetadataId);
	void			Remove(uint32 Index);
	FItemHandle		ReadItems() const;
	FItemHandle		NextItem(FItemHandle Handle) const;
	int32			GetItemIndex(FItemHandle Handle) const;

private:
	struct FEntry
	{
		uint64		Address;
		uint32		Serial;		// this may overflow on long runs. There's looooads of bits free in 'Address' though.
		uint32		MetadataId;
	};

	enum
	{
		BucketShift = 8,
		BucketSize	= 1 << BucketShift,
		BucketMask	= BucketSize - 1,
		Tomb		= 1,
		EntryAlign	= 64,
	};

	static uint64	GetHash(uint64 Address);
	void			AddInternal(uint64 Address, uint32 Serial, uint32 MetadataId);
	uint32			GetBucketIndex(uint64 Hash) const;
	FEntry*			Data = nullptr;
	uint32			Num = 0;
	uint32			Capacity = 0;
	uint32			NumTombs = 0;
};

////////////////////////////////////////////////////////////////////////////////
inline uint64 FLaneItemSet::GetAddress(uint32 Index) const
{
	return Data[Index].Address;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FLaneItemSet::GetSerial(uint32 Index) const
{
	return Data[Index].Serial;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FLaneItemSet::GetMetadataId(uint32 Index) const
{
	return Data[Index].MetadataId;
}

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
