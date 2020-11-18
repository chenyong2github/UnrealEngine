// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "FastUpdate/SlateInvalidationWidgetIndex.h"
#include "FastUpdate/SlateInvalidationWidgetList.h"
#include "FastUpdate/SlateInvalidationWidgetSortOrder.h"

/**
 * Ordered list of WidgetIndex. The order is based on the WidgetSortIndex.
 */
class FSlateInvalidationWidgetHeap
{
public:
	using FElement = TPair<FSlateInvalidationWidgetIndex, FSlateInvalidationWidgetSortOrder>;
	struct TWidgetOrderGreater
	{
		FORCEINLINE bool operator()(const FElement& A, const FElement& B) const
		{
			return B.Get<1>() < A.Get<1>();
		}
	};

public:
	FSlateInvalidationWidgetHeap(FSlateInvalidationWidgetList& InOwnerList)
		: OwnerList(InOwnerList)
	{ }

	/** Insert into the list at the proper order (see binary heap) only if it's not already contains by the list. */
	void PushUnique(const FSlateInvalidationWidgetIndex WidgetIndex)
	{
		FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = OwnerList[WidgetIndex];
		check(Contains(WidgetIndex) == InvalidationWidget.bContainedByWidgetHeap);
		if (!InvalidationWidget.bContainedByWidgetHeap)
		{
			InvalidationWidget.bContainedByWidgetHeap = true;
			InvalidationWidget.bInUpdateList = true;
			Heap.HeapPush(FElement{ WidgetIndex, FSlateInvalidationWidgetSortOrder{OwnerList, WidgetIndex} }, TWidgetOrderGreater());
		}
	}

	/** Insert into the list at the proper order (see binary heap) only if it's not already contains by the list. */
	void PushUnique(FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
	{
		check(Contains(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetHeap);

		if (!InvalidationWidget.bContainedByWidgetHeap)
		{
			InvalidationWidget.bContainedByWidgetHeap = true;
			InvalidationWidget.bInUpdateList = true;
			Heap.HeapPush(FElement{ InvalidationWidget.Index, FSlateInvalidationWidgetSortOrder{OwnerList, InvalidationWidget.Index} }, TWidgetOrderGreater());
		}
	}

	/** Insert into the list at the proper order (see binary heap). */
	void ForcePush(const FSlateInvalidationWidgetIndex WidgetIndex)
	{
		FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = OwnerList[WidgetIndex];

		InvalidationWidget.bContainedByWidgetHeap = true;
		InvalidationWidget.bInUpdateList = true;
		Heap.HeapPush(FElement{ WidgetIndex, FSlateInvalidationWidgetSortOrder{OwnerList, WidgetIndex} }, TWidgetOrderGreater());
	}

	/** Returns and removes the biggest WidgetIndex from the list. */
	FSlateInvalidationWidgetIndex Pop()
	{
		FSlateInvalidationWidgetIndex Result = Heap.HeapTop().Get<0>();
		Heap.HeapPopDiscard(TWidgetOrderGreater(), false);
		OwnerList[Result].bContainedByWidgetHeap = false;
		return Result;
	}

	/** Empties the list, but doesn't change memory allocations. */
	inline void Reset(bool bResetContained)
	{
		if (bResetContained)
		{
			for (const FElement& Element : Heap)
			{
				OwnerList[Element.Get<0>()].bContainedByWidgetHeap = false;
			}
		}
		Heap.Reset();
	}
	
	/** Returns the number of elements in the list. */
	inline int32 Num() const
	{
		return Heap.Num();
	}

	/** Returns the raw list. */
	const TArray<FElement, TInlineAllocator<100>>& GetRaw() const
	{
		return Heap;
	}

private:
	bool Contains(const FSlateInvalidationWidgetIndex WidgetIndex) const
	{
		return Heap.ContainsByPredicate([WidgetIndex](const FElement& Element)
			{
				return Element.Get<0>() == WidgetIndex;
			});
	}

private:
	TArray<FElement, TInlineAllocator<100>> Heap;
	FSlateInvalidationWidgetList& OwnerList;
};