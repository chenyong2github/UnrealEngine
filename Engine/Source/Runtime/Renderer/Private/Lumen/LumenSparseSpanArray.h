// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenSparseSpanArray.h:
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"

/**
 * Grow only sparse array with stable indices and contiguous span allocation.
 */
template <typename ElementType>
class TSparseSpanArray
{
public:
	int32 Num() const
	{
		return Elements.Num();
	}

	void Reserve(int32 NumElements)
	{
		Elements.Reserve(NumElements);
	}

	int32 AddSpan(int32 NumElements)
	{
		check(NumElements > 0);
		// First try to allocate from the free spans.
		int32 InsertIndex = AllocateFromFreeSpans(NumElements);

		if (InsertIndex == -1)
		{
			// Allocate new span.
			InsertIndex = Elements.Num();
			Elements.AddDefaulted(NumElements);
			AllocatedElementsBitArray.Add(true, NumElements);
		}
		else
		{
			// Reuse existing span.
			for (int32 ElementIndex = InsertIndex; ElementIndex < InsertIndex + NumElements; ++ElementIndex)
			{
				checkSlow(!IsAllocated(ElementIndex));
				Elements[ElementIndex] = ElementType();
			}

			AllocatedElementsBitArray.SetRange(InsertIndex, NumElements, true);
		}

		return InsertIndex;
	}

	void RemoveSpan(int32 FirstElementIndex, int32 NumElements)
	{
		check(NumElements > 0);

		for (int32 ElementIndex = FirstElementIndex; ElementIndex < FirstElementIndex + NumElements; ++ElementIndex)
		{
			checkSlow(IsAllocated(ElementIndex));

			Elements[ElementIndex].~ElementType();
		}

		// #lumen_todo: implement defragmentation and array size shrinking.
		RemoveFromFreeSpans(FirstElementIndex, NumElements);
		AllocatedElementsBitArray.SetRange(FirstElementIndex, NumElements, false);

		const int32 TrimmedNumElements = TrimFreeSpans();

		check(Elements.Num() == AllocatedElementsBitArray.Num());
		Elements.SetNum(TrimmedNumElements, false);
		AllocatedElementsBitArray.SetNumUninitialized(TrimmedNumElements);
	}

	void Reset()
	{
		for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
		{
			if (IsAllocated(ElementIndex))
			{
				Elements[ElementIndex].~ElementType();
			}
		}

		FreeSpans.Reset();
		Elements.Reset();
		AllocatedElementsBitArray.SetNumUninitialized(0);
	}

	ElementType& operator[](int32 Index)
	{
		checkSlow(IsAllocated(Index));
		return Elements[Index];
	}

	const ElementType& operator[](int32 Index) const
	{
		checkSlow(IsAllocated(Index));
		return Elements[Index];
	}

	bool IsAllocated(int32 ElementIndex) const
	{
		if (ElementIndex < AllocatedElementsBitArray.Num())
		{
			return AllocatedElementsBitArray[ElementIndex];
		}

		return false;
	}

	SIZE_T GetAllocatedSize() const
	{
		return Elements.GetAllocatedSize() + AllocatedElementsBitArray.GetAllocatedSize() + FreeSpans.GetAllocatedSize();
	}

	class TRangedForIterator
	{
	public:
		TRangedForIterator(TSparseSpanArray<ElementType>& InArray, int32 InElementIndex)
			: Array(InArray)
			, ElementIndex(InElementIndex)
		{
			// Scan for the first valid element.
			while (ElementIndex < Array.Elements.Num() && !Array.AllocatedElementsBitArray[ElementIndex])
			{
				++ElementIndex;
			} 
		}

		TRangedForIterator operator++()
		{
			// Scan for the next first valid element.
			do
			{
				++ElementIndex;
			} while (ElementIndex < Array.Elements.Num() && !Array.AllocatedElementsBitArray[ElementIndex]);

			return *this;
		}

		bool operator!=(const TRangedForIterator& Other) const
		{
			return ElementIndex != Other.ElementIndex;
		}

		ElementType& operator*()
		{
			return Array.Elements[ElementIndex];
		}

	private:
		TSparseSpanArray<ElementType>& Array;
		int32 ElementIndex;
	};

	class TRangedForConstIterator
	{
	public:
		TRangedForConstIterator(const TSparseSpanArray<ElementType>& InArray, int32 InElementIndex)
			: Array(InArray)
			, ElementIndex(InElementIndex)
		{
			// Scan for the first valid element.
			while (ElementIndex < Array.Elements.Num() && !Array.AllocatedElementsBitArray[ElementIndex])
			{
				++ElementIndex;
			}
		}

		TRangedForConstIterator operator++()
		{ 
			// Scan for the next first valid element.
			do
			{
				++ElementIndex;
			} while (ElementIndex < Array.Elements.Num() && !Array.AllocatedElementsBitArray[ElementIndex]);

			return *this;
		}

		bool operator!=(const TRangedForConstIterator& Other) const
		{ 
			return ElementIndex != Other.ElementIndex;
		}

		const ElementType& operator*() const
		{ 
			return Array.Elements[ElementIndex];
		}

	private:
		const TSparseSpanArray<ElementType>& Array;
		int32 ElementIndex;
	};

	/**
	 * Iterate over all allocated elements (skip free ones).
	 */
	TRangedForIterator begin() { return TRangedForIterator(*this, 0); }
	TRangedForIterator end() { return TRangedForIterator(*this, Elements.Num()); }
	TRangedForConstIterator begin() const { return TRangedForConstIterator(*this, 0); }
	TRangedForConstIterator end() const { return TRangedForConstIterator(*this, Elements.Num()); }

private:

	class FSpan
	{
	public:
		FSpan()
		{
		}

		FSpan(int32 InFirstElementIndex, int32 InNumElements)
			: FirstElementIndex(InFirstElementIndex)
			, NumElements(InNumElements)
		{
		}

		int32 FirstElementIndex;
		int32 NumElements;
	};

	TArray<ElementType> Elements;
	TBitArray<> AllocatedElementsBitArray;
	TArray<FSpan> FreeSpans;

	int32 AllocateFromFreeSpans(int32 NumElements)
	{
		for (int32 FreeSpanIndex = 0; FreeSpanIndex < FreeSpans.Num(); ++FreeSpanIndex)
		{
			FSpan& FreeSpan = FreeSpans[FreeSpanIndex];
			if (FreeSpan.NumElements >= NumElements)
			{
				const int32 InsertIndex = FreeSpan.FirstElementIndex;

				FreeSpan.FirstElementIndex += NumElements;
				FreeSpan.NumElements -= NumElements;

				if (FreeSpan.NumElements <= 0)
				{
					FreeSpans.RemoveAt(FreeSpanIndex);
				}

				return InsertIndex;
			}
		}

		return -1;
	}

	void RemoveFromFreeSpans(int32 FirstElementIndex, int32 NumElements)
	{
		bool bAdded = false;
		int32 NextFreeSpanIndex = 0;
		for (int32 FreeSpanIndex = 0; FreeSpanIndex < FreeSpans.Num(); ++FreeSpanIndex)
		{
			FSpan& FreeSpan = FreeSpans[FreeSpanIndex];

			if (FreeSpan.FirstElementIndex + FreeSpan.NumElements < FirstElementIndex)
			{
				NextFreeSpanIndex = FreeSpanIndex + 1;
			}

			if (FreeSpan.FirstElementIndex == FirstElementIndex + NumElements)
			{
				FreeSpan.FirstElementIndex = FirstElementIndex;
				FreeSpan.NumElements += NumElements;
				bAdded = true;
				break;
			}

			if (FreeSpan.FirstElementIndex + FreeSpan.NumElements == FirstElementIndex)
			{
				FreeSpan.NumElements += NumElements;

				// Try to merge two ranges.
				if (FreeSpanIndex + 1 < FreeSpans.Num())
				{
					FSpan& NextFreeSpan = FreeSpans[FreeSpanIndex + 1];
					if (FreeSpan.FirstElementIndex + FreeSpan.NumElements == NextFreeSpan.FirstElementIndex)
					{
						FreeSpan.NumElements += NextFreeSpan.NumElements;
						FreeSpans.RemoveAt(FreeSpanIndex + 1);
					}
				}

				bAdded = true;
				break;
			}
		}

		if (!bAdded)
		{
			FreeSpans.Insert(FSpan(FirstElementIndex, NumElements), NextFreeSpanIndex);
		}
	}

	int32 TrimFreeSpans()
	{
		int32 NewSize = Elements.Num();

		// Try to remove last element of the free span list and resize the free span list.
		if (FreeSpans.Num() > 0)
		{
			FSpan& FreeSpan = FreeSpans.Last();
			if (FreeSpan.FirstElementIndex + FreeSpan.NumElements == Elements.Num())
			{
				NewSize = FreeSpan.FirstElementIndex;
				FreeSpans.Pop();
			}
		}

		return NewSize;
	}
};