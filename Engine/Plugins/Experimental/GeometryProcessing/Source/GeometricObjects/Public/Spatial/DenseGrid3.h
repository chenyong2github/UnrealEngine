// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp DenseGrid3

#pragma once

#include "CoreMinimal.h"
#include "BoxTypes.h"

#include "HAL/PlatformAtomics.h"


/**
 * 3D dense grid of floating-point scalar values. 
 */
template<typename ElemType>
class TDenseGrid3
{
protected:
	/** grid of allocated elements */
	TArray<ElemType> Buffer;

	/** dimensions per axis */
	FVector3i Dimensions;

public:
	/**
	 * Create empty grid
	 */
	TDenseGrid3() : Dimensions(0,0,0)
	{
	}

	TDenseGrid3(int DimX, int DimY, int DimZ, ElemType InitialValue)
	{
		Resize(DimX, DimY, DimZ);
		Assign(InitialValue);
	}

	int Size() const
	{
		return Dimensions.X * Dimensions.Y * Dimensions.Z;
	}

	bool IsValidIndex(const FVector3i& Index) const
	{
		return Index.X >= 0 && Index.Y >= 0 && Index.Z >= 0
			&& Index.X < Dimensions.X && Index.Y < Dimensions.Y && Index.Z < Dimensions.Z;
	}

	const FVector3i& GetDimensions() const
	{
		return Dimensions;
	}

	void Resize(int DimX, int DimY, int DimZ, bool bAllowShrinking = true)
	{
		check((int64)DimX * (int64)DimY * (int64)DimZ < INT_MAX);
		Buffer.SetNumUninitialized(DimX * DimY * DimZ, bAllowShrinking);
		Dimensions = FVector3i(DimX, DimY, DimZ);
	}

	void Assign(ElemType Value)
	{
		for (int i = 0; i < Buffer.Num(); ++i)
		{
			Buffer[i] = Value;
		}
	}

	void SetMin(const FVector3i& IJK, ElemType F)
	{
		int Idx = IJK.X + Dimensions.X * (IJK.Y + Dimensions.Y * IJK.Z);
		if (F < Buffer[Idx])
		{
			Buffer[Idx] = F;
		}
	}
	void SetMax(const FVector3i& IJK, ElemType F)
	{
		int Idx = IJK.X + Dimensions.X * (IJK.Y + Dimensions.Y * IJK.Z);
		if (F > Buffer[Idx])
		{
			Buffer[Idx] = F;
		}
	}

	constexpr ElemType& operator[](int Idx)
	{
		return Buffer[Idx];
	}
	constexpr const ElemType& operator[](int Idx) const
	{
		return Buffer[Idx];
	}

	constexpr ElemType& operator[](FVector3i Idx)
	{
		return Buffer[Idx.X + Dimensions.X * (Idx.Y + Dimensions.Y * Idx.Z)];
	}
	constexpr const ElemType& operator[](FVector3i Idx) const
	{
		return Buffer[Idx.X + Dimensions.X * (Idx.Y + Dimensions.Y * Idx.Z)];
	}
	constexpr ElemType& At(int I, int J, int K)
	{
		return Buffer[I + Dimensions.X * (J + Dimensions.Y * K)];
	}
	constexpr const ElemType& At(int I, int J, int K) const
	{
		return Buffer[I + Dimensions.X * (J + Dimensions.Y * K)];
	}


	void GetXPair(int X0, int Y, int Z, ElemType& AOut, ElemType& BOut) const
	{
		int Offset = Dimensions.X * (Y + Dimensions.Y * Z);
		AOut = Buffer[Offset + X0];
		BOut = Buffer[Offset + X0 + 1];
	}

	void Apply(TFunctionRef<ElemType(ElemType)> F)
	{
		for (int Idx = 0, Num = Size(); Idx < Num; Idx++)
		{
			Buffer[Idx] = F(Buffer[Idx]);
		}
	}

	// TODO: implement TDenseGrid2<ElemType> and then implement this
	//TDenseGrid2<ElemType> get_slice(int slice_i, int dimension)
	//{
	//	TDenseGrid2<ElemType> slice;
	//	if (dimension == 0) {
	//		slice = TDenseGrid2<ElemType>(Dimensions.Y, Dimensions.Z, 0);
	//		for (int k = 0; k < Dimensions.Z; ++k)
	//			for (int j = 0; j < Dimensions.Y; ++j)
	//				slice[j, k] = Buffer[slice_i + Dimensions.X * (j + Dimensions.Y * k)];
	//	} else if (dimension == 1) {
	//		slice = TDenseGrid2<ElemType>(Dimensions.X, Dimensions.Z, 0);
	//		for (int k = 0; k < Dimensions.Z; ++k)
	//			for (int i = 0; i < Dimensions.X; ++i)
	//				slice[i, k] = Buffer[i + Dimensions.X * (slice_i + Dimensions.Y * k)];
	//	} else {
	//		slice = TDenseGrid2<ElemType>(Dimensions.X, Dimensions.Y, 0);
	//		for (int j = 0; j < Dimensions.Y; ++j)
	//			for (int i = 0; i < Dimensions.X; ++i)
	//				slice[i, j] = Buffer[i + Dimensions.X * (j + Dimensions.Y * slice_i)];
	//	}
	//	return slice;
	//}
	//void set_slice(TDenseGrid2<ElemType> slice, int slice_i, int dimension)
	//{
	//	if (dimension == 0) {
	//		for (int k = 0; k < Dimensions.Z; ++k)
	//			for (int j = 0; j < Dimensions.Y; ++j)
	//				Buffer[slice_i + Dimensions.X * (j + Dimensions.Y * k)] = slice[j, k];
	//	} else if (dimension == 1) {
	//		for (int k = 0; k < Dimensions.Z; ++k)
	//			for (int i = 0; i < Dimensions.X; ++i)
	//				Buffer[i + Dimensions.X * (slice_i + Dimensions.Y * k)] = slice[i, k];
	//	} else {
	//		for (int j = 0; j < Dimensions.Y; ++j)
	//			for (int i = 0; i < Dimensions.X; ++i)
	//				Buffer[i + Dimensions.X * (j + Dimensions.Y * slice_i)] = slice[i, j];
	//	}
	//}


	FAxisAlignedBox3i Bounds() const
	{
		return FAxisAlignedBox3i({0, 0, 0},{Dimensions.X, Dimensions.Y, Dimensions.Z});
	}
	FAxisAlignedBox3i BoundsInclusive() const
	{
		return FAxisAlignedBox3i({0, 0, 0},{Dimensions.X-1, Dimensions.Y-1, Dimensions.Z-1});
	}

	FVector3i ToIndex(int Idx) const
	{
		int x = Idx % Dimensions.X;
		int y = (Idx / Dimensions.X) % Dimensions.Y;
		int z = Idx / (Dimensions.X * Dimensions.Y);
		return FVector3i(x, y, z);
	}
	int ToLinear(int X, int Y, int Z) const
	{
		return X + Dimensions.X * (Y + Dimensions.Y * Z);
	}
	int ToLinear(const FVector3i& IJK) const
	{
		return IJK.X + Dimensions.X * (IJK.Y + Dimensions.Y * IJK.Z);
	}
};


typedef TDenseGrid3<float> FDenseGrid3f;
typedef TDenseGrid3<double> FDenseGrid3d;
typedef TDenseGrid3<int> FDenseGrid3i;

// additional utility functions
namespace DenseGrid
{

	inline void AtomicIncrement(FDenseGrid3i& Grid, int i, int j, int k)
	{
		FPlatformAtomics::InterlockedIncrement(&Grid.At(i,j,k));
	}

	inline void AtomicDecrement(FDenseGrid3i& Grid, int i, int j, int k)
	{
		FPlatformAtomics::InterlockedDecrement(&Grid.At(i, j, k));
	}

	inline void AtomicIncDec(FDenseGrid3i& Grid, int i, int j, int k, bool bDecrement = false)
	{
		if (bDecrement)
		{
			FPlatformAtomics::InterlockedDecrement(&Grid.At(i, j, k));
		}
		else
		{
			FPlatformAtomics::InterlockedIncrement(&Grid.At(i, j, k));
		}
	}
}

