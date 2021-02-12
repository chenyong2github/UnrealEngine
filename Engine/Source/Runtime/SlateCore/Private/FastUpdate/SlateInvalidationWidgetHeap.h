// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "FastUpdate/SlateInvalidationWidgetIndex.h"
#include "FastUpdate/SlateInvalidationWidgetList.h"
#include "FastUpdate/SlateInvalidationWidgetSortOrder.h"


namespace UE
{
namespace Slate
{
namespace Private
{
	/**
	* Ordered list of WidgetIndex. The order is based on the WidgetSortIndex.
	*/
	struct FSlateInvalidationWidgetHeapElement
	{
	public:
		FSlateInvalidationWidgetHeapElement(FSlateInvalidationWidgetIndex InIndex, FSlateInvalidationWidgetSortOrder InSortOrder)
			: WidgetIndex(InIndex), WidgetSortOrder(InSortOrder)
		{}
		inline FSlateInvalidationWidgetIndex GetWidgetIndex() const { return WidgetIndex; }
		inline FSlateInvalidationWidgetIndex& GetWidgetIndexRef() { return WidgetIndex; }
		inline FSlateInvalidationWidgetSortOrder GetWidgetSortOrder() const { return WidgetSortOrder; }
		inline FSlateInvalidationWidgetSortOrder& GetWidgetSortOrderRef() { return WidgetSortOrder; }

	private:
		FSlateInvalidationWidgetIndex WidgetIndex;
		FSlateInvalidationWidgetSortOrder WidgetSortOrder;
	};
} // Private
} // Slate
} // UE

/**
 * Heap of widget that is ordered by increasing sort order. The list need to always stay ordered.
 */
class FSlateInvalidationWidgetPreHeap
{
public:
	using FElement = UE::Slate::Private::FSlateInvalidationWidgetHeapElement;
	struct FWidgetOrderLess
	{
		FORCEINLINE bool operator()(const FElement& A, const FElement& B) const
		{
			return A.GetWidgetSortOrder() < B.GetWidgetSortOrder();
		}
	};
	using SortPredicate = FWidgetOrderLess;
	
	static constexpr int32 NumberOfPreAllocatedElement = 32;
	using FElementContainer = TArray<FElement, TInlineAllocator<NumberOfPreAllocatedElement>>;

	FSlateInvalidationWidgetPreHeap(FSlateInvalidationWidgetList& InOwnerList)
		: OwnerList(InOwnerList)
	{ }

public:
	/** Insert into the list at the proper order (see binary heap) only if it's not already contains by the list. */
	void HeapPushUnique(FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
	{
		check(InvalidationWidget.Index != FSlateInvalidationWidgetIndex::Invalid);
		check(Contains_Debug(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetPreHeap);

		if (!InvalidationWidget.bContainedByWidgetPreHeap)
		{
			InvalidationWidget.bContainedByWidgetPreHeap = true;
			Heap.HeapPush(FElement{ InvalidationWidget.Index, FSlateInvalidationWidgetSortOrder{OwnerList, InvalidationWidget.Index} }, SortPredicate());
		}
	}

	/** Returns and removes the biggest WidgetIndex from the list. */
	FSlateInvalidationWidgetIndex HeapPop()
	{
		FSlateInvalidationWidgetIndex Result = Heap.HeapTop().GetWidgetIndex();
		Heap.HeapPopDiscard(SortPredicate(), false);
		OwnerList[Result].bContainedByWidgetPreHeap = false;
		return Result;
	}
	
	/** Returns the biggest WidgetIndex from the list. */
	inline FSlateInvalidationWidgetIndex HeapPeek() const
	{
		return Heap.HeapTop().GetWidgetIndex();
	}
	
	/** Returns the biggest WidgetIndex from the list. */
	inline const FElement& HeapPeekElement() const
	{
		return Heap.HeapTop();
	}

	/** Remove range */
	int32 RemoveRange(const FSlateInvalidationWidgetList::FIndexRange& Range)
	{
		int32 RemoveCount = 0;
		for (int32 Index = 0; Index < Heap.Num(); ++Index)
		{
			if (Range.Include(Heap[Index].GetWidgetSortOrder()))
			{
				const FSlateInvalidationWidgetIndex WidgetIndex = Heap[Index].GetWidgetIndex();
				OwnerList[WidgetIndex].bContainedByWidgetPreHeap = false;
				Heap.HeapRemoveAt(Index, SortPredicate());
				Index = INDEX_NONE; // start again
			}
		}
		return RemoveCount;
	}

	/** Empties the list, but doesn't change memory allocations. */
	void Reset(bool bResetContained)
	{
		if (bResetContained)
		{
			for (const FElement& Element : Heap)
			{
				OwnerList[Element.GetWidgetIndex()].bContainedByWidgetPreHeap = false;
			}
		}

		// Destroy the second allocator, if it exists.
		Heap.Empty(NumberOfPreAllocatedElement);
	}

	/** Returns the number of elements in the list. */
	inline int32 Num() const
	{
		return Heap.Num();
	}

	/** Does it contains the widget index. */
	bool Contains_Debug(const FSlateInvalidationWidgetIndex WidgetIndex) const
	{
		return Heap.ContainsByPredicate([WidgetIndex](const FElement& Element)
			{
				return Element.GetWidgetIndex() == WidgetIndex;
			});
	}

	/** @retuns true if the heap is heapified. */
	inline bool IsValidHeap_Debug()
	{
		return Algo::IsHeap(Heap, SortPredicate());
	}

	/** Returns the raw list. */
	inline const FElementContainer& GetRaw() const
	{
		return Heap;
	}

	/** Iterate over each element in the list in any order. */
	template<typename Predicate>
	void ForEachIndexes(Predicate Pred)
	{
		for (FElement& Element : Heap)
		{
			Pred(Element);
		}
	}

private:
	FElementContainer Heap;
	FSlateInvalidationWidgetList& OwnerList;
};


/** */
class FSlateInvalidationWidgetPostHeap
{
public:
	using FElement = UE::Slate::Private::FSlateInvalidationWidgetHeapElement;
	struct FWidgetOrderGreater
	{
		FORCEINLINE bool operator()(const FElement& A, const FElement& B) const
		{
			return B.GetWidgetSortOrder() < A.GetWidgetSortOrder();
		}
	};
	using SortPredicate = FWidgetOrderGreater;
	
	static constexpr int32 NumberOfPreAllocatedElement = 100;
	using FElementContainer = TArray<FElement, TInlineAllocator<NumberOfPreAllocatedElement>>;

	FSlateInvalidationWidgetPostHeap(FSlateInvalidationWidgetList& InOwnerList)
		: OwnerList(InOwnerList)
		, WidgetCannotBeAdded(FSlateInvalidationWidgetIndex::Invalid)
		, bIsHeap(false)
	{ }

public:
	/** Insert into the list at the proper order (see binary heap) but only if it's not already contains by the list. */
	void HeapPushUnique(FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
	{
		check(bIsHeap);
		check(InvalidationWidget.Index != FSlateInvalidationWidgetIndex::Invalid);
		check(Contains_Debug(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetPostHeap
			|| WidgetCannotBeAdded == InvalidationWidget.Index);

		if (!InvalidationWidget.bContainedByWidgetPostHeap)
		{
			InvalidationWidget.bContainedByWidgetPostHeap = true;
			Heap.HeapPush(FElement{ InvalidationWidget.Index, FSlateInvalidationWidgetSortOrder{OwnerList, InvalidationWidget.Index} }, SortPredicate());
		}
	}

	/** Insert at the end of the list but only if it's not already contains by the list. */
	void PushBackUnique(FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
	{
		check(bIsHeap == false);
		check(InvalidationWidget.Index != FSlateInvalidationWidgetIndex::Invalid);
		check(Contains_Debug(InvalidationWidget.Index) == InvalidationWidget.bContainedByWidgetPostHeap
			|| WidgetCannotBeAdded == InvalidationWidget.Index);

		if (!InvalidationWidget.bContainedByWidgetPostHeap)
		{
			InvalidationWidget.bContainedByWidgetPostHeap = true;
			Heap.Emplace(InvalidationWidget.Index, FSlateInvalidationWidgetSortOrder{OwnerList, InvalidationWidget.Index});
		}
	}

	/** PushBackUnique or PushHeapUnique depending if the list is Heapified. */
	void PushBackOrHeapUnique(FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget)
	{
		if (bIsHeap)
		{
			HeapPushUnique(InvalidationWidget);
		}
		else
		{
			PushBackUnique(InvalidationWidget);
		}
	}

	/** Returns and removes the biggest WidgetIndex from the list. */
	FSlateInvalidationWidgetIndex HeapPop()
	{
		check(bIsHeap == true);
		FSlateInvalidationWidgetIndex Result = Heap.HeapTop().GetWidgetIndex();
		Heap.HeapPopDiscard(SortPredicate(), false);
		OwnerList[Result].bContainedByWidgetPostHeap = false;
		return Result;
	}

	/** Remove range */
	int32 RemoveRange(const FSlateInvalidationWidgetList::FIndexRange& Range)
	{
		check(bIsHeap == false);
		int32 RemoveCount = 0;
		for (int32 Index = Heap.Num() -1; Index >= 0 ; --Index)
		{
			if (Range.Include(Heap[Index].GetWidgetSortOrder()))
			{
				const FSlateInvalidationWidgetIndex WidgetIndex = Heap[Index].GetWidgetIndex();
				OwnerList[WidgetIndex].bContainedByWidgetPostHeap = false;
				Heap.RemoveAtSwap(Index);
				++RemoveCount;
			}
		}

		return RemoveCount;
	}

	/** Empties the list, but doesn't change memory allocations. */
	void Reset(bool bResetContained)
	{
		if (bResetContained)
		{
			for (const FElement& Element : Heap)
			{
				OwnerList[Element.GetWidgetIndex()].bContainedByWidgetPostHeap = false;
			}
		}

		// Destroy the second allocator, if it exists.
		Heap.Empty(NumberOfPreAllocatedElement);
		bIsHeap = false;
	}

	void Heapify()
	{
		check(!bIsHeap);
		Heap.Heapify(SortPredicate());
		bIsHeap = true;
	}

	/** Returns the number of elements in the list. */
	inline int32 Num() const
	{
		return Heap.Num();
	}

	/** Returns the number of elements in the list. */
	inline bool IsHeap() const
	{
		return bIsHeap;
	}

	/** Does it contains the widget index. */
	bool Contains_Debug(const FSlateInvalidationWidgetIndex WidgetIndex) const
	{
		return Heap.ContainsByPredicate([WidgetIndex](const FElement& Element)
			{
				return Element.GetWidgetIndex() == WidgetIndex;
			});
	}

	/** @retuns true if the heap is heapified. */
	inline bool IsValidHeap_Debug()
	{
		return Algo::IsHeap(Heap, SortPredicate());
	}

	/** Returns the raw list. */
	inline const FElementContainer& GetRaw() const
	{
		return Heap;
	}

	template<typename Predicate>
	void ForEachIndexes(Predicate Pred)
	{
		for (FElement& Element : Heap)
		{
			Pred(Element);
		}
	}
	
public:
	struct FScopeWidgetCannotBeAdded
	{
		FScopeWidgetCannotBeAdded(FSlateInvalidationWidgetPostHeap& InHeap, FSlateInvalidationWidgetList::InvalidationWidgetType& InInvalidationWidget)
			: Heap(InHeap)
			, InvalidationWidget(InInvalidationWidget)
			, WidgetIndex(InvalidationWidget.Index)
		{
			check(!InvalidationWidget.bContainedByWidgetPostHeap
				&& Heap.WidgetCannotBeAdded == FSlateInvalidationWidgetIndex::Invalid);
			Heap.WidgetCannotBeAdded = InvalidationWidget.Index;
			InvalidationWidget.bContainedByWidgetPostHeap = true;
		}
		~FScopeWidgetCannotBeAdded()
		{
			Heap.WidgetCannotBeAdded = FSlateInvalidationWidgetIndex::Invalid;
			check(Heap.OwnerList.IsValidIndex(WidgetIndex));
			check(&Heap.OwnerList[WidgetIndex] == &InvalidationWidget);
			InvalidationWidget.bContainedByWidgetPostHeap = false;
		}
		FSlateInvalidationWidgetPostHeap& Heap;
		FSlateInvalidationWidgetList::InvalidationWidgetType& InvalidationWidget;
		FSlateInvalidationWidgetIndex WidgetIndex;
	};

private:
	FElementContainer Heap;
	FSlateInvalidationWidgetList& OwnerList;
	FSlateInvalidationWidgetIndex WidgetCannotBeAdded;
	bool bIsHeap;
};
