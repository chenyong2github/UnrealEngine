// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Containers/Array.h"
#include "Algo/BinarySearch.h"

namespace Chaos
{
	// Index based view, specialized for working with several ranges within a same array such as particles.
	template<typename TItemsType>
	class TPBDActiveView
	{
	public:
		TPBDActiveView(TItemsType& InItems)
			: Items(InItems)
		{}

		// Return all items, including those not in the view.
		TItemsType& GetItems() const { return Items; }

		//// Return the total number of items in the view.
		//int32 GetSize() const { return Size; }

		// Add a new active (or inactive) range at the end of the list, and return its offset.
		int32 AddRange(int32 NumItems, bool bActivate = true);

		// Return the number of items in the range starting at the specified offset.
		int32 GetRangeSize(int32 Offset) const;

		// Activate (or deactivate) the range starting at the specified offset.
		void ActivateRange(int32 Offset, bool bActivate);

		// Execute the specified function on all active items.
		void SequentialFor(TFunctionRef<void(TItemsType&, int32)> Function) const;

		// Execute the specified function in parallel on all active items. Set MinParallelSize to run sequential on the smaller ranges.
		void ParallelFor(TFunctionRef<void(TItemsType&, int32)> Function, int32 MinParallelSize = TNumericLimits<int32>::Max()) const;

		// Remove all ranges above the current given size.
		void Reset(int32 Offset = 0);

		// Return whether there is any active range in the view.
		bool HasActiveRange() const;

	private:
		TItemsType& Items;
		TArray<int32> Ranges;
	};

	template <class TItemsType>
	int32 TPBDActiveView<TItemsType>::AddRange(int32 NumItems, bool bActivate)
	{
		const int32 Offset = Ranges.Num() ? FMath::Abs(Ranges.Last()) : 0;
		if (NumItems)
		{
			const int32 Size = Offset + NumItems;
			Ranges.Emplace(bActivate ? Size : -Size);
		}
		return Offset;
	}

	template <class TItemsType>
	int32 TPBDActiveView<TItemsType>::GetRangeSize(int32 Offset) const
	{
		// Binary search upper bound range of this offset
		const int32 Index = Algo::UpperBound(Ranges, Offset, [](const int32 A, const int32 B) { return FMath::Abs(A) < FMath::Abs(B); });
		check(Ranges.IsValidIndex(Index));

		// Return size regardless or activation state
		return FMath::Abs(Ranges[Index]) - Offset;
	}

	template <class TItemsType>
	void TPBDActiveView<TItemsType>::ActivateRange(int32 Offset, bool bActivate)
	{
		// Binary search upper bound range of this offset
		const int32 Index = Algo::UpperBound(Ranges, Offset, [](const int32 A, const int32 B) { return FMath::Abs(A) < FMath::Abs(B); });
		check(Ranges.IsValidIndex(Index));

		// Change activation state (sign) if needed
		if ((Ranges[Index] > 0) != bActivate)
		{
			Ranges[Index] = -Ranges[Index];
		}
	}

	template <class TItemsType>
	void TPBDActiveView<TItemsType>::SequentialFor(TFunctionRef<void(TItemsType&, int32)> Function) const
	{
		int32 Offset = 0;
		for (int32 Range : Ranges)
		{
			if (Range > 0)
			{
				// Active range
				for (int32 Index = Offset; Index < Range; ++Index)
				{
					Function(Items, Index);
				}
				Offset = Range;
			}
			else
			{
				// Inactive range
				Offset = -Range;
			}
		}
	}

	template <class TItemsType>
	void TPBDActiveView<TItemsType>::ParallelFor(TFunctionRef<void(TItemsType&, int32)> Function, int32 MinParallelBatchSize) const
	{
		int32 Offset = 0;
		for (int32 Range : Ranges)
		{
			if (Range > 0)
			{
				// Active range
				const int32 RangeSize = Range - Offset;
				PhysicsParallelFor(RangeSize, [this, Offset, &Function](int32 Index)
				{
					Function(Items, Offset + Index);
				}, /*bForceSingleThreaded =*/ RangeSize < MinParallelBatchSize);
				Offset = Range;
			}
			else
			{
				// Inactive range
				Offset = -Range;
			}
		}
	}

	template <class TItemsType>
	void TPBDActiveView<TItemsType>::Reset(int32 Offset)
	{
		for (int32 Index = 0; Index < Ranges.Num(); ++Index)
		{
			if (FMath::Abs(Ranges[Index]) > Offset)
			{
				Ranges.SetNum(Index);
				break;
			}
		}
	}

	template <class TItemsType>
	bool TPBDActiveView<TItemsType>::HasActiveRange() const
	{
		for (int32 Range : Ranges)
		{
			if (Range > 0)
			{
				return true;
			}
		}
		return false;
	}
}
