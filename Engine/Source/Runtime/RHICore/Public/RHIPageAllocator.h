// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHI.h"

struct FVRamPageRange
{
	uint32 StartIndex = 0;
	uint32 Count = 0;
};

/** A non-contiguous span allocator for pages. */
class FRHIPageAllocator
{
	struct FPageSpan
	{
		const bool IsLinked() { return (NextSpanIndex >= 0 || PrevSpanIndex >= 0); }

		int32 StartPageIndex = -1;
		int32 Count = 0;
		int32 NextSpanIndex = -1;
		int32 PrevSpanIndex = 0;
		bool bAllocated = false;
	};

	static const int32 FREE_SPAN_LIST_HEAD_INDEX = 0;
	static const int32 FREE_SPAN_LIST_TAIL_INDEX = 1;
public:
	static const int32 INVALID_INDEX = -1;
	typedef int32 Handle;

	FRHIPageAllocator()
		: MaxSpanCount(514)
		, PageCount(512)
	{
		Init();
	}

	FRHIPageAllocator(uint32 InPageCount)
		: MaxSpanCount(InPageCount + 2)
		, PageCount(InPageCount)
	{
		Init();
	}

	void Reset();

	// Allocates pages, returning a span index (or INVALID_INDEX on failure), allows partial allocations, but fails if zero pages were available
	Handle AllocPagesPartial(int32 Count, uint32& NumPagesAllocated, const TCHAR* DebugName);

	// Allocates pages, returning a span index (or INVALID_INDEX on failure)
	Handle AllocPages(int32 Count, const TCHAR* DebugName = nullptr);

	// Frees previously allocated pages by span index
	void FreePages(Handle SpanIndex);

	// Generates a flag array of pages for a given SpanIndex
	void GetPageArray(Handle SpanIndex, TArray<uint32>& PagesOut, uint32 PageOffset = 0, bool bAppend = false);

	// Generates an array of contiguous ranges
	void GetRangeArray(Handle SpanIndex, TArray<FVRamPageRange>& RangesOut, uint32 PageOffset = 0, bool bAppend = false);

	// For debugging/profiling - returns the number of spans
	uint32 GetSpanCount() const
	{
		return MaxSpanCount - UnusedSpanListCount - 2;
	}

	// For debugging/profiling - returns the number of outstanding allocations
	uint32 GetAllocationCount() const
	{
		return AllocationCount;
	}

	uint32 GetFreePageCount() const
	{
		return uint32(FreePageCount);
	}

	// For debugging - returns the start offset. Note that a given allocation is not necessarily contiguous
	uint32 GetAllocationStartPage(Handle SpanIndex) const
	{
		check((uint32)SpanIndex < MaxSpanCount);
		check(PageSpans[SpanIndex].bAllocated);
		return PageSpans[SpanIndex].StartPageIndex;
	}

	// For debugging - returns the size of this allocation in pages
	uint32 GetAllocationPageCount(Handle SpanIndex) const
	{
		check((uint32)SpanIndex < MaxSpanCount);
		check(PageSpans[SpanIndex].bAllocated);
		uint32 Count = 0;
		for (; SpanIndex != -1; SpanIndex = PageSpans[SpanIndex].NextSpanIndex)
		{
			Count += PageSpans[SpanIndex].Count;
		}
		return Count;
	}

	int32 GetMaxSpanCount() const
	{
		return MaxSpanCount;
	}

private:
	void Init();

	// Allocates pages
	Handle AllocPagesInternal(int32 Count, uint32& NumPagesAllocated, bool bAllowPartialAlloc, const TCHAR* DebugName);

	// Splits a span into two, so that the original span has PageCount pages and the new span contains the remaining ones
	void SplitSpan(int32 SpanIndex, int32 PageCount);

	// Merges two spans. They must be adjacent and in the same list
	void MergeSpans(int32 SpanIndex0, int32 SpanIndex1, const bool bKeepSpan1);

	// Inserts a span after an existing span. The span to insert must be unlinked
	void InsertAfter(int32 InsertPosition, int32 InsertSpanIndex);

	// Inserts a span after an existing span. The span to insert must be unlinked
	void InsertBefore(int32 InsertPosition, int32 InsertSpanIndex);

	// Removes a span from its list, reconnecting neighbouring list elements
	void Unlink(int32 SpanIndex);

	// Allocates an unused span from the pool
	int AllocSpan()
	{
		check(UnusedSpanListCount > 0);
		int32 SpanIndex = UnusedSpanList[UnusedSpanListCount - 1];
		UnusedSpanListCount--;
		return SpanIndex;
	}

	// Releases a span back to the unused pool
	void ReleaseSpan(int32 SpanIndex)
	{
		check(!PageSpans[SpanIndex].IsLinked());
		UnusedSpanList[UnusedSpanListCount] = SpanIndex;
		UnusedSpanListCount++;
		check(UnusedSpanListCount <= (int32)PageCount);
	}

	// Merges a span with existing neighbours in the free list if they exist
	bool MergeFreeSpanIfPossible(int32 SpanIndex);

	void Validate();

	int GetFirstSpanIndex() const
	{
		return PageSpans[FREE_SPAN_LIST_HEAD_INDEX].NextSpanIndex;
	}

	int32 FreePageCount;
	TArray<int32> PageToSpanStart;  // [PAGE_COUNT + 1]
	TArray<int32> PageToSpanEnd;    // [PAGE_COUNT + 1]

	TArray<FPageSpan> PageSpans;    // [MAX_SPAN_COUNT]
	TArray<int32> UnusedSpanList;	// [MAX_SPAN_COUNT]
	int32 UnusedSpanListCount;

	const uint32 MaxSpanCount;
	const uint32 PageCount;
	uint32 AllocationCount;
};