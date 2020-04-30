// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <CoreMinimal.h>
#include "IndexTypes.h"
#include "VectorTypes.h"


/**
* FElementLinearization maps a potentially-sparse index list into a linear array.
* Used linearize things like VtxIds of a mesh as a single array and allow bidirectional mapping between array offset and mesh VtxId.
* Linearized array offset can then be used for things like matrix row indexing when building a Laplacian matrix.
*/
class FElementLinearization
{
public:
	FElementLinearization() = default;

	// Lookup   ToVtxId(Index) = VtxId;
	const TArray<int32>& ToId() const { return ToIdMap; }

	// Lookup   ToIndex(VtxId) = Index;  may return FDynamicMesh3::InvalidID 
	const TArray<int32>& ToIndex() const { return ToIndexMap; }

	int32 NumIds() const { return ToIdMap.Num(); }

	// Following the FDynamicMesh3 convention this is really MaxId + 1
	int32 MaxId() const { return ToIndexMap.Num(); }

	void Empty() { ToIdMap.Empty();  ToIndexMap.Empty(); }

	template<typename IterableType>
	void Populate(const int32 MaxId, const int32 Count, IterableType Iterable)
	{
		ToIndexMap.SetNumUninitialized(MaxId);
		ToIdMap.SetNumUninitialized(Count);

		for (int32 i = 0; i < MaxId; ++i)
		{
			ToIndexMap[i] = IndexConstants::InvalidID;
		}

		// create the mapping
		{
			int32 N = 0;
			for (int32 Id : Iterable)
			{
				ToIdMap[N] = Id;
				ToIndexMap[Id] = N;
				N++;
			}
		}
	}

protected:
	TArray<int32>  ToIdMap;
	TArray<int32>  ToIndexMap;

private:
	FElementLinearization(const FElementLinearization&);
};



/**
 * Structure-of-Array (SoA) storage for a list of 3-vectors
 */
template<typename RealType>
class TVector3Arrays
{
protected:
	TArray<RealType> XVector;
	TArray<RealType> YVector;
	TArray<RealType> ZVector;

public:

	TVector3Arrays(int32 Size)
	{
		XVector.SetNum(Size);
		YVector.SetNum(Size);
		ZVector.SetNum(Size);
	}

	TVector3Arrays()
	{}

	void SetZero(int32 NumElements)
	{
		XVector.Reset(0);
		XVector.SetNumZeroed(NumElements, false);
		YVector.Reset(0);
		YVector.SetNumZeroed(NumElements, false);
		ZVector.Reset(0);
		ZVector.SetNumZeroed(NumElements, false);
	}

	// Test that all the arrays have the same given size.
	bool bHasSize(int32 Size) const
	{
		return (XVector.Num() == Size && YVector.Num() == Size && ZVector.Num() == Size);
	}

	int32 Num() const
	{
		int32 Size = XVector.Num();
		if (!bHasSize(Size))
		{
			Size = -1;
		}
		return Size;
	}

	RealType X(int32 i) const
	{
		return XVector[i];
	}

	RealType Y(int32 i) const
	{
		return YVector[i];
	}

	RealType Z(int32 i) const
	{
		return ZVector[i];
	}

	void SetX(int32 i, const RealType& Value)
	{
		XVector[i] = Value;
	}

	void SetY(int32 i, const RealType& Value)
	{
		YVector[i] = Value;
	}

	void SetZ(int32 i, const RealType& Value)
	{
		ZVector[i] = Value;
	}

	void SetXYZ(int32 i, const FVector3<RealType>& Value)
	{
		XVector[i] = Value.X;
		YVector[i] = Value.Y;
		ZVector[i] = Value.Z;
	}
};

