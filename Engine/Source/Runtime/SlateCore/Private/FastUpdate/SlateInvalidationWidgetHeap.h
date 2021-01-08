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
		, WidgetCannotBeAdded(FSlateInvalidationWidgetIndex::Invalid)
	{ }

	/** Insert into the list at the proper order (see binary heap) only if it's not already contains by the list. */
	void PushUnique(const FSlateInvalidationWidgetIndex WidgetIndex)
	{
		check(WidgetIndex != FSlateInvalidationWidgetIndex::Invalid);

		FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget = OwnerList[WidgetIndex];
		check(Contains(WidgetIndex) == InvalidationWidget.bContainedByWidgetHeap
			|| WidgetCannotBeAdded == WidgetIndex);

		if (!InvalidationWidget.bContainedByWidgetHeap)
		{
			InvalidationWidget.bContainedByWidgetHeap = true;
			Heap.HeapPush(FElement{ WidgetIndex, FSlateInvalidationWidgetSortOrder{OwnerList, WidgetIndex} }, TWidgetOrderGreater());
		}
	}

	/** Insert into the list at the proper order (see binary heap) only if it's not already contains by the list. */
	void PushUnique(FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
	{
		check(InvalidationWidget.Index != FSlateInvalidationWidgetIndex::Invalid);

		check(Contains(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetHeap
			|| WidgetCannotBeAdded == InvalidationWidget.Index);

		if (!InvalidationWidget.bContainedByWidgetHeap)
		{
			InvalidationWidget.bContainedByWidgetHeap = true;
			Heap.HeapPush(FElement{ InvalidationWidget.Index, FSlateInvalidationWidgetSortOrder{OwnerList, InvalidationWidget.Index} }, TWidgetOrderGreater());
		}
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

public:
	struct FScopeWidgetCannotBeAdded
	{
		FScopeWidgetCannotBeAdded(FSlateInvalidationWidgetHeap& InHeap, FSlateInvalidationWidgetList::InvalidationWidgetType& InInvalidationWidget)
			: Heap(InHeap)
			, InvalidationWidget(InInvalidationWidget)
			, WidgetIndex(InvalidationWidget.Index)
		{
			check(!InvalidationWidget.bContainedByWidgetHeap
				&& Heap.WidgetCannotBeAdded == FSlateInvalidationWidgetIndex::Invalid);
			Heap.WidgetCannotBeAdded = InvalidationWidget.Index;
			InvalidationWidget.bContainedByWidgetHeap = true;
		}
		~FScopeWidgetCannotBeAdded()
		{
			Heap.WidgetCannotBeAdded = FSlateInvalidationWidgetIndex::Invalid;
			check(Heap.OwnerList.IsValidIndex(WidgetIndex));
			check(&Heap.OwnerList[WidgetIndex] == &InvalidationWidget);
			InvalidationWidget.bContainedByWidgetHeap = false;
		}
		FSlateInvalidationWidgetHeap& Heap;
		FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget;
		FSlateInvalidationWidgetIndex WidgetIndex;
	};

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
	FSlateInvalidationWidgetIndex WidgetCannotBeAdded;
};
