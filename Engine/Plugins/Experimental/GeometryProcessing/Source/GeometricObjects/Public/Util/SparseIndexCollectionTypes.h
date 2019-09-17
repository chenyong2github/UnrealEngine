// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// partial port of geometry3sharp Indexing.cs

#pragma once

#include <CoreMinimal.h>


/**
 *  This class provides optionally sparse or dense boolean flags for a set of integer indices
 */
class FIndexFlagSet
{
private:
	TOptional<TArray<bool>> Dense; // TODO: consider switching to bit array; but test first as performance of bit array may by significantly worse
	int DenseCount;      // only tracked for Dense
	TOptional<TSet<int>> Sparse;

public:
	FIndexFlagSet(bool bSetSparse = true, int MaxIndex = -1)
	{
		InitManual(bSetSparse, MaxIndex);
	}

	FIndexFlagSet(int MaxIndex, int SubsetCountEst)
	{
		InitAuto(MaxIndex, SubsetCountEst);
	}

	void InitAuto(int MaxIndex, int SubsetCountEst)
	{
		bool bSmall = MaxIndex < 32000;
		constexpr float PercentThresh = 0.05f;           

		InitManual(!bSmall && ((float)SubsetCountEst / (float)MaxIndex < PercentThresh), MaxIndex);
	}

	void InitManual(bool bSetSparse, int MaxIndex = -1)
	{
		if (bSetSparse)
		{
			Sparse = TSet<int>();
		}
		else
		{
			Dense = TArray<bool>();
			check(MaxIndex >= 0);
			Dense->SetNumZeroed(FMath::Max(0, MaxIndex));
		}
		DenseCount = 0;
	}

	/**
	 *  checks if value i is true
	 */
	FORCEINLINE bool Contains(int i) const
	{
		if (Dense.IsSet())
		{
			return Dense.GetValue()[i];
		}
		else
		{
			check(Sparse.IsSet());
			return Sparse->Contains(i);
		}
	}

	/**
	 *  sets value i to true
	 */
	FORCEINLINE void Add(int i)
	{
		if (Dense.IsSet())
		{
			bool &Value = Dense.GetValue()[i];
			if (!Value)
			{
				Value = true;
				DenseCount++;
			}
		}
		else
		{
			check(Sparse.IsSet());
			Sparse->Add(i);
		}
	}

	FORCEINLINE void Remove(int Index)
	{
		if (Dense.IsSet())
		{
			bool &Value = Dense.GetValue()[Index];
			if (Value)
			{
				Value = false;
				DenseCount--;
			}
		}
		else
		{
			check(Sparse.IsSet());
			Sparse->Remove(Index);
		}
	}

	/**
	 *  Returns number of true values in set
	 */
	FORCEINLINE int Count() const
	{
		if (Dense.IsSet())
		{
			return DenseCount;
		}
		else
		{
			return Sparse->Num();
		}
	}


	inline const bool operator[](unsigned int Index) const
	{
		return Contains(Index);
	}
	

	// TODO: iterator support

};




// TODO implement a similar class but as a template map
//template<typename T>
//class TOptionallySparseMap
//{
// etc
//
//};
