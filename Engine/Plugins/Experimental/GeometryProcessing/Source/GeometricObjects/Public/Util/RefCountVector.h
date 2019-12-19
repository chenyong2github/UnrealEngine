// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp FRefCountVector

#pragma once

#include "CoreMinimal.h"
#include "Util/DynamicVector.h"
#include "Util/IteratorUtil.h"


/**
 * FRefCountVector is used to keep track of which indices in a linear Index list are in use/referenced.
 * A free list is tracked so that unreferenced indices can be re-used.
 * 
 * The enumerator iterates over valid indices (ie where refcount > 0)
 * @warning refcounts are 16-bit ints (shorts) so the maximum count is 65536. behavior is undefined if this overflows.
 * @warning No overflow checking is done in release builds.
 */
class FRefCountVector
{
public:
	static constexpr short INVALID_REF_COUNT = -1;

	FRefCountVector() = default;
	FRefCountVector(const FRefCountVector&) = default;
	FRefCountVector(FRefCountVector&&) = default;
	FRefCountVector& operator=(const FRefCountVector&) = default;
	FRefCountVector& operator=(FRefCountVector&&) = default;

	bool IsEmpty() const
	{
		return UsedCount == 0;
	}

	size_t GetCount() const
	{
		return UsedCount;
	}

	size_t GetMaxIndex() const
	{
		return RefCounts.GetLength();
	}

	bool IsDense() const
	{
		return FreeIndices.GetLength() == 0;
	}

	bool IsValid(int Index) const
	{
		return (Index >= 0 && Index < (int)RefCounts.GetLength() && RefCounts[Index] > 0);
	}

	bool IsValidUnsafe(int Index) const
	{
		return RefCounts[Index] > 0;
	}

	int GetRefCount(int Index) const
	{
		int n = RefCounts[Index];
		return (n == INVALID_REF_COUNT) ? 0 : n;
	}

	int GetRawRefCount(int Index) const
	{
		return RefCounts[Index];
	}

	int Allocate()
	{
		UsedCount++;
		if (FreeIndices.IsEmpty())
		{
			// [RMS] do we need this branch anymore?
			RefCounts.Add(1);
			return (int)RefCounts.GetLength() - 1;
		}
		else
		{
			int iFree = INVALID_REF_COUNT;
			while (iFree == INVALID_REF_COUNT && FreeIndices.IsEmpty() == false)
			{
				iFree = FreeIndices.Back();
				FreeIndices.PopBack();
			}
			if (iFree != INVALID_REF_COUNT)
			{
				RefCounts[iFree] = 1;
				return iFree;
			}
			else
			{
				RefCounts.Add(1);
				return (int)RefCounts.GetLength() - 1;
			}
		}
	}

	int Increment(int Index, short IncrementCount = 1)
	{
		check(IsValid(Index));
		// debug check for overflow...
		check((short)(RefCounts[Index] + IncrementCount) > 0);
		RefCounts[Index] += IncrementCount;
		return RefCounts[Index];
	}

	void Decrement(int Index, short DecrementCount = 1)
	{
		check(IsValid(Index));
		RefCounts[Index] -= DecrementCount;
		check(RefCounts[Index] >= 0);
		if (RefCounts[Index] == 0)
		{
			FreeIndices.Add(Index);
			RefCounts[Index] = INVALID_REF_COUNT;
			UsedCount--;
		}
	}

	/**
	 * allocate at specific Index, which must either be larger than current max Index,
	 * or on the free list. If larger, all elements up to this one will be pushed onto
	 * free list. otherwise we have to do a linear search through free list.
	 * If you are doing many of these, it is likely faster to use
	 * AllocateAtUnsafe(), and then RebuildFreeList() after you are done.
	 */
	bool AllocateAt(int Index)
	{
		if (Index >= (int)RefCounts.GetLength())
		{
			int j = (int)RefCounts.GetLength();
			while (j < Index)
			{
				int InvalidCount = INVALID_REF_COUNT;	// required on older clang because a constexpr can't be passed by ref
				RefCounts.Add(InvalidCount);
				FreeIndices.Add(j);
				++j;
			}
			RefCounts.Add(1);
			UsedCount++;
			return true;
		}
		else
		{
			if (RefCounts[Index] > 0)
			{
				return false;
			}
			int N = (int)FreeIndices.GetLength();
			for (int i = 0; i < N; ++i)
			{
				if (FreeIndices[i] == Index)
				{
					FreeIndices[i] = INVALID_REF_COUNT;
					RefCounts[Index] = 1;
					UsedCount++;
					return true;
				}
			}
			return false;
		}
	}

	/**
	 * allocate at specific Index, which must be free or larger than current max Index.
	 * However, we do not update free list. So, you probably need to do RebuildFreeList() after calling this.
	 */
	bool AllocateAtUnsafe(int Index)
	{
		if (Index >= (int)RefCounts.GetLength())
		{
			int j = (int)RefCounts.GetLength();
			while (j < Index)
			{
				int InvalidCount = INVALID_REF_COUNT;	// required on older clang because a constexpr can't be passed by ref
				RefCounts.Add(InvalidCount);
				++j;
			}
			RefCounts.Add(1);
			UsedCount++;
			return true;

		}
		else
		{
			if (RefCounts[Index] > 0)
			{
				return false;
			}
			RefCounts[Index] = 1;
			UsedCount++;
			return true;
		}
	}

	const TDynamicVector<short>& GetRawRefCounts() const
	{
		return RefCounts;
	}

	/**
	 * @warning you should not use this!
	 */
	TDynamicVector<short>& GetRawRefCountsUnsafe()
	{
		return RefCounts;
	}

	/**
	 * @warning you should not use this!
	 */
	void SetRefCountUnsafe(int Index, short ToCount)
	{
		RefCounts[Index] = ToCount;
	}

	// todo:
	//   remove
	//   clear

	void RebuildFreeList()
	{
		FreeIndices = TDynamicVector<int>();
		UsedCount = 0;

		int N = (int)RefCounts.GetLength();
		for (int i = 0; i < N; ++i) 
		{
			if (RefCounts[i] > 0)
			{
				UsedCount++;
			}
			else
			{
				FreeIndices.Add(i);
			}
		}
	}

	void Trim(int maxIndex)
	{
		FreeIndices = TDynamicVector<int>();
		RefCounts.Resize(maxIndex);
		UsedCount = maxIndex;
	}

	//
	// Iterators
	//

	/**
	 * base iterator for indices with valid refcount (skips zero-refcount indices)
	 */
	class BaseIterator
	{
	public:
		inline BaseIterator()
		{
			Vector = nullptr;
			Index = 0;
			LastIndex = 0;
		}

		inline bool operator==(const BaseIterator& Other) const
		{
			return Index == Other.Index;
		}
		inline bool operator!=(const BaseIterator& Other) const
		{
			return Index != Other.Index;
		}

	protected:
		inline void goto_next()
		{
			if (Index != LastIndex)
			{
				Index++;
			}
			while (Index != LastIndex && Vector->IsValidUnsafe(Index) == false)
			{
				Index++;
			}
		}

		inline BaseIterator(const FRefCountVector* VectorIn, int IndexIn, int LastIn)
		{
			Vector = VectorIn;
			Index = IndexIn;
			LastIndex = LastIn;
			if (Index != LastIndex && Vector->IsValidUnsafe(Index) == false)
			{
				goto_next();		// initialize
			}
		}
		const FRefCountVector * Vector;
		int Index;
		int LastIndex;
		friend class FRefCountVector;
	};

	/*
	 *  iterator over valid indices (ie non-zero refcount)
	 */
	class IndexIterator : public BaseIterator
	{
	public:
		inline IndexIterator() : BaseIterator() {}

		inline int operator*() const
		{
			return this->Index;
		}

		inline IndexIterator & operator++() 		// prefix
		{
			this->goto_next();
			return *this;
		}
		inline IndexIterator operator++(int) 		// postfix
		{
			IndexIterator copy(*this);
			this->goto_next();
			return copy;
		}

	protected:
		inline IndexIterator(const FRefCountVector* VectorIn, int Index, int Last) : BaseIterator(VectorIn, Index, Last)
		{}
		friend class FRefCountVector;
	};

	inline IndexIterator BeginIndices() const
	{
		return IndexIterator(this, (int)0, (int)RefCounts.GetLength());
	}

	inline IndexIterator EndIndices() const
	{
		return IndexIterator(this, (int)RefCounts.GetLength(), (int)RefCounts.GetLength());
	}

	/**
	 * enumerable object that provides begin()/end() semantics, so
	 * you can iterate over valid indices using range-based for loop
	 */
	class IndexEnumerable
	{
	public:
		const FRefCountVector* Vector;
		IndexEnumerable() { Vector = nullptr; }
		IndexEnumerable(const FRefCountVector* VectorIn) { Vector = VectorIn; }
		typename FRefCountVector::IndexIterator begin() const { return Vector->BeginIndices(); }
		typename FRefCountVector::IndexIterator end() const { return Vector->EndIndices(); }
	};

	/**
	 * returns iteration object over valid indices
	 * usage: for (int idx : indices()) { ... }
	 */
	inline IndexEnumerable Indices() const
	{
		return IndexEnumerable(this);
	}


	/*
	 * enumerable object that maps indices output by Index_iteration to a second type
	 */
	template<typename ToType>
	class MappedEnumerable
	{
	public:
		TFunction<ToType(int)> MapFunc;
		IndexEnumerable enumerable;

		MappedEnumerable(const IndexEnumerable& enumerable, TFunction<ToType(int)> MapFunc)
		{
			this->enumerable = enumerable;
			this->MapFunc = MapFunc;
		}

		MappedIterator<int, ToType, IndexIterator> begin()
		{
			return MappedIterator<int, ToType, IndexIterator>(enumerable.begin(), MapFunc);
		}

		MappedIterator<int, ToType, IndexIterator> end()
		{
			return MappedIterator<int, ToType, IndexIterator>(enumerable.end(), MapFunc);
		}
	};


	/**
	* returns iteration object over mapping applied to valid indices
	* eg usage: for (FVector3d v : mapped_indices(fn_that_looks_up_mesh_vtx_from_id)) { ... }
	*/
	template<typename ToType>
	inline MappedEnumerable<ToType> MappedIndices(TFunction<ToType(int)> MapFunc) const
	{
		return MappedEnumerable<ToType>(Indices(), MapFunc);
	}

	/*
	* iteration object that maps indices output by Index_iteration to a second type
	*/
	class FilteredEnumerable
	{
	public:
		TFunction<bool(int)> FilterFunc;
		IndexEnumerable enumerable;
		FilteredEnumerable(const IndexEnumerable& enumerable, TFunction<bool(int)> FilterFuncIn)
		{
			this->enumerable = enumerable;
			this->FilterFunc = FilterFuncIn;
		}

		FilteredIterator<int, IndexIterator> begin()
		{
			return FilteredIterator<int, IndexIterator>(enumerable.begin(), enumerable.end(), FilterFunc);
		}

		FilteredIterator<int, IndexIterator> end()
		{
			return FilteredIterator<int, IndexIterator>(enumerable.end(), enumerable.end(), FilterFunc);
		}
	};

	inline FilteredEnumerable FilteredIndices(TFunction<bool(int)> FilterFunc) const
	{
		return FilteredEnumerable(Indices(), FilterFunc);
	}

	FString UsageStats()
	{
		return FString::Printf(TEXT("RefCountSize %d  FreeSize %d  FreeMem %dkb"),
			RefCounts.GetLength(), FreeIndices.GetLength(), (FreeIndices.GetByteCount() / 1024));
	}

private:
	TDynamicVector<short> RefCounts{};
	TDynamicVector<int> FreeIndices{};
	int UsedCount{0};


};
