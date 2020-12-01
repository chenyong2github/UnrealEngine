// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Support.h"

namespace Trace {
namespace TraceServices {

////////////////////////////////////////////////////////////////////////////////
inline uint32 Sbif_GetMaxDepth(uint32 TotalColumns)
{
	/* we're always expecting a non-zero value */
	return 32 - UnsafeCountLeadingZeros(TotalColumns);
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 Sbif_GetCellAtDepth(uint32 Column, uint32 Depth)
{
	uint32 LeafIndex = Column * 2;
	uint32 k = 1 << Depth;
	return (LeafIndex & ~k) | (k - 1);
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 Sbif_GetCommonDepth(uint32 ColumnA, uint32 ColumnB)
{
	uint32 Xor = ColumnA ^ ColumnB;
	return Sbif_GetMaxDepth(Xor);
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 Sbif_GetCellWidth(uint32 CellIndex)
{
	return ((CellIndex ^ (CellIndex + 1)) >> 1) + 1;
}

////////////////////////////////////////////////////////////////////////////////
inline uint32 Sbif_GetBaseColumn(uint32 Column, uint32 Depth)
{
	return Column & ~((1 << Depth) - 1);
}

////////////////////////////////////////////////////////////////////////////////
inline void Sbif_GetColumnDepthFromCell(uint32 CellIndex, uint32* Column, uint32* Depth)
{
	uint32 PlusOne = CellIndex + 1;
	uint32 LeastUnset = PlusOne ^ -int32(PlusOne);
	*Column = (CellIndex & PlusOne) >> 1;
	*Depth = 31 - UnsafeCountLeadingZeros(LeastUnset);
}

} // namespace TraceServices
} // namespace Trace

/* vim: set noet : */
