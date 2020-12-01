// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SbifIdentities.inl"

namespace Trace {
namespace TraceServices {

class FMetadataDb;

////////////////////////////////////////////////////////////////////////////////
class FSbifContext
{
public:
				FSbifContext(uint32 CellIndex, uint32 ColumnShift);
	uint32		GetColumn() const;
	uint32		GetDepth() const;
	uint32		GetCellIndex() const;

private:
				FSbifContext() = default;
	uint32		Column;
	uint32		Depth;
	uint32		BaseSerial;

	friend		class FTracker;
	friend		class FSbifRetiree;
};

////////////////////////////////////////////////////////////////////////////////
inline FSbifContext::FSbifContext(uint32 CellIndex, uint32 ColumnShift)
{
	Sbif_GetColumnDepthFromCell(CellIndex, &Column, &Depth);

	uint32 BaseColumn = Sbif_GetBaseColumn(Column, Depth);
	BaseSerial = BaseColumn << ColumnShift;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FSbifContext::GetColumn() const
{
	return Column;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FSbifContext::GetDepth() const
{
	return Depth;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FSbifContext::GetCellIndex() const
{
	return Sbif_GetCellAtDepth(Column, Depth);
}



////////////////////////////////////////////////////////////////////////////////
class FSbifRetiree
{
public:
	uint64				GetAddress() const;
	uint32				GetStartSerial(const FSbifContext& Context) const;
	uint32				GetEndSerial(const FSbifContext& Context) const;
	uint32				GetDuration(const FSbifContext& Context) const;
	uint32				GetMetadataId() const;

private:
	friend				class FTracker;
	uint32				_SpaceForAddress; // change metadata from an ID to an index to make space.
	uint32				StartSerial;
	uint32				EndSerial;
	uint32				MetadataId;
};
static_assert(sizeof(FSbifRetiree), "Designed to be XMM-sized");

////////////////////////////////////////////////////////////////////////////////
inline uint64 FSbifRetiree::GetAddress() const
{
	// More work required to free up 12 more bits to store 44 bits active in an
	// allocation's address. Where do we get 12 more bits from? Refactor the
	// metadata ID to be an index instead of a hash. Use Context to leverage
	// detail to encode start/end better.
	return _SpaceForAddress;
}

////////////////////////////////////////////////////////////////////////////////
/*
inline uint32 FSbifRetiree::GetMidSerial(const FSbifContext& Context) const
{
	return Context.BaseSerial + MidSerialOffset;
}
*/

////////////////////////////////////////////////////////////////////////////////
inline uint32 FSbifRetiree::GetDuration(const FSbifContext& Context) const
{
	// return Context.MinDuration + DurationTopup;
	return GetEndSerial(Context) - GetStartSerial(Context);
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FSbifRetiree::GetStartSerial(const FSbifContext& Context) const
{
	//return GetMidSerial(Context) - ((GetDuration(Context) + 1) >> 1);
	return StartSerial + Context.BaseSerial;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FSbifRetiree::GetEndSerial(const FSbifContext& Context) const
{
	//return GetMidSerial(Context) + (GetDuration(Context) >> 1);
	return EndSerial + Context.BaseSerial;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 FSbifRetiree::GetMetadataId() const
{
	return MetadataId;
}



////////////////////////////////////////////////////////////////////////////////
class ISbifBuilder
{
public:
	virtual uint32		GetEventsPerColumn() const = 0;
	virtual void		Begin(const FMetadataDb* MetadataDb) = 0;
	virtual void		End() = 0;
	virtual void		AddColumn() = 0;
	virtual void		AddRetirees(const FSbifContext* Context, const FSbifRetiree* Retirees, uint32 Num) = 0;
};

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
