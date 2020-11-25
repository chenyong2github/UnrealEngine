// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AllocationsProvider.h"
//#include "Common/PagedArray.h"

namespace TraceServices
{

class ILinearAllocator;

////////////////////////////////////////////////////////////////////////////////////////////////////

class FSbTreeUtils
{
public:
	static uint32 GetMaxDepth(uint32 TotalColumns);
	static uint32 GetCellAtDepth(uint32 Column, uint32 Depth);
	static uint32 GetCommonDepth(uint32 ColumnA, uint32 ColumnB);
	static uint32 GetCellWidth(uint32 CellIndex);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FSbTreeCell
{
public:
	FSbTreeCell(ILinearAllocator& InAllocator);

	uint32 GetAllocCount() const { return Allocs.Num(); }
	void AddAlloc(const FAllocationItem* Alloc);

	double GetMinStartTime() const { return MinStartTime; }
	double GetMaxEndTime() const { return MaxEndTime; }

	void Query(TArray<const FAllocationItem*>& OutAllocs, const IAllocationsProvider::FQueryParams& Params) const;

private:
	ILinearAllocator& Allocator;

	//TODO: TPagedArray<FAllocationItem> Allocs;
	TArray<FAllocationItem> Allocs;

	uint32 MinStartEventIndex;
	uint32 MaxEndEventIndex;
	double MinStartTime;
	double MaxEndTime;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

class FSbTree
{
public:
	FSbTree(ILinearAllocator& InAllocator, uint32 InColumnShift);
	~FSbTree();

	void SetTimeForEvent(uint32 EventIndex, double Time);

	void AddAlloc(const FAllocationItem* Alloc);

	uint32 GetColumnWidth() const { return 1 << ColumnShift; }
	uint32 GetCurrentColumn() const { return CurrentColumn; }

	int32 GetColumnAtTime(double Time) const;

	void Query(TArray<const FSbTreeCell*>& OutCells, const IAllocationsProvider::FQueryParams& Params) const;

	void IterateCells(TArray<const FSbTreeCell*>& OutCells, int32 Column) const;
	void IterateCells(TArray<const FSbTreeCell*>& OutCells, int32 StartColumn, int32 EndColumn) const;

	void DebugPrint() const;

private:
	ILinearAllocator& Allocator;
	TArray<FSbTreeCell*> Cells;
	TArray<FSbTreeCell*> OffsettedCells;
	TArray<double> ColumnStartTimes;
	uint32 ColumnShift; // ColumnWidth = 1 << ColumnShift
	uint32 CurrentColumn;
};

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace TraceServices
