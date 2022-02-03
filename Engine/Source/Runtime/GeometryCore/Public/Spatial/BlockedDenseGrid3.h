// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3Sharp BlockedDenseGrid3

#pragma once

#include "CoreMinimal.h"
#include "BoxTypes.h"
#include "IntBoxTypes.h"
#include "Containers/StaticArray.h"

namespace UE
{
namespace Geometry
{

/**
 * TBlockedDenseGrid represents a dense 3D uniform grid, but the grid is allocated in BlockSize^3 blocks on-demand
 * (BlockSize is a compile-time constant). This allows very large grids to be used without
 * having to pre-allocate all the memory, eg for sparse/narrow-band use cases.
 * 
 * Thread-safe access functions are provided, these are implemented by internal locking on the grid-block level.
 */
template<typename ElemType>
class TBlockedDenseGrid3
{
protected:

	static const int32 BlockSize = 32;
	using BlockType = TStaticArray<ElemType, BlockSize*BlockSize*BlockSize>;

	TArray<TUniquePtr<BlockType>> Blocks;
	TArray<FCriticalSection> BlockLocks;

	/** dimensions per axis */
	FVector3i BlockDimensions;

	/** dimensions per axis */
	FVector3i Dimensions;

	/** The value to use when no value is known - ie for Get() queries on unallocated cells, or to initialize new blocks */
	ElemType ConstantValue = (ElemType)0;

protected:

	void Init(BlockType& Block)
	{
		for (int32 k = 0; k < BlockSize * BlockSize * BlockSize; ++k)
		{
			Block[k] = ConstantValue;
		}
	}

	void GetBlockCoords(int32 X, int32 Y, int32 Z, int32& BlockIndexOut, int32& LocalIndexOut) const
	{
		int32 BlockX = X / BlockSize;
		int32 BlockY = Y / BlockSize;
		int32 BlockZ = Z / BlockSize;
		BlockIndexOut = BlockX + BlockDimensions.X * (BlockY + BlockDimensions.Y * BlockZ);

		int32 LocalX = X % BlockSize;
		int32 LocalY = Y % BlockSize;
		int32 LocalZ = Z % BlockSize;
		LocalIndexOut = LocalX + BlockSize * (LocalY + BlockSize * LocalZ);
	}

#if UE_BUILD_DEBUG
	TUniquePtr<BlockType>& GetBlock(int32 Index) { return Blocks[Index]; }
	const TUniquePtr<BlockType>& GetBlock(int32 Index) const { return Blocks[Index]; }
	FCriticalSection* GetBlockLock(int32 Index) { return &BlockLocks[Index]; }
#else
	// skip range checks in non-debug builds
	TUniquePtr<BlockType>& GetBlock(int32 Index) { return Blocks.GetData()[Index]; }
	const TUniquePtr<BlockType>& GetBlock(int32 Index) const { return Blocks.GetData()[Index]; }
	FCriticalSection* GetBlockLock(int32 Index) { return &BlockLocks.GetData()[Index]; }
#endif

	template<typename FuncType>
	void WriteValueThreadSafe(int32 X, int32 Y, int32 Z, FuncType Func)
	{
		int32 BlockIndex, LocalIndex;
		GetBlockCoords(X, Y, Z, BlockIndex, LocalIndex);

		FScopeLock Lock(GetBlockLock(BlockIndex));

		if (GetBlock(BlockIndex).IsValid() == false)
		{
			Blocks[BlockIndex] = MakeUnique<BlockType>();
			Init(*Blocks[BlockIndex]);
		}
		TUniquePtr<BlockType>& Block = GetBlock(BlockIndex);
		ElemType& Value = (*Block)[LocalIndex];
		Func(Value);
	}


	template<typename FuncType>
	void WriteValue(int32 X, int32 Y, int32 Z, FuncType Func)
	{
		int32 BlockIndex, LocalIndex;
		GetBlockCoords(X, Y, Z, BlockIndex, LocalIndex);

		if (GetBlock(BlockIndex).IsValid() == false)
		{
			Blocks[BlockIndex] = MakeUnique<BlockType>();
			Init(*Blocks[BlockIndex]);
		}
		TUniquePtr<BlockType>& Block = GetBlock(BlockIndex);
		ElemType& Value = (*Block)[LocalIndex];
		Func(Value);
	}


	ElemType ReadValueThreadSafe(int32 X, int32 Y, int32 Z)
	{
		int32 BlockIndex, LocalIndex;
		GetBlockCoords(X, Y, Z, BlockIndex, LocalIndex);

		FScopeLock Lock(GetBlockLock(BlockIndex));
		const TUniquePtr<BlockType>& Block = GetBlock(BlockIndex);
		return (Block.IsValid()) ? (*Block)[LocalIndex] : ConstantValue;
	}

	ElemType ReadValue(int32 X, int32 Y, int32 Z) const
	{
		int32 BlockIndex, LocalIndex;
		GetBlockCoords(X, Y, Z, BlockIndex, LocalIndex);

		const TUniquePtr<BlockType>& Block = GetBlock(BlockIndex);
		return (Block.IsValid()) ? (*Block)[LocalIndex] : ConstantValue;
	}


public:
	/**
	 * Create empty grid
	 */
	TBlockedDenseGrid3() : Dimensions(0,0,0)
	{
	}

	TBlockedDenseGrid3(int32 DimX, int32 DimY, int32 DimZ, ElemType InitialValue)
	{
		Resize(DimX, DimY, DimZ);
		ConstantValue = InitialValue;
	}

	int64 Size() const
	{
		return (int64)Dimensions.X * (int64)Dimensions.Y * (int64)Dimensions.Z;
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

	/**
	 * Reconfigure the grid to have the target dimensions. This clears all the 
	 * existing grid memory.
	 */
	void Resize(int32 DimX, int32 DimY, int32 DimZ)
	{
		check((int64)DimX * (int64)DimY * (int64)DimZ < INT_MAX);
		int32 BlocksX = (DimX / BlockSize) + 1;
		int32 BlocksY = (DimY / BlockSize) + 1;
		int32 BlocksZ = (DimZ / BlockSize) + 1;

		Blocks.Reset();
		int32 NumBlocks = BlocksX * BlocksY * BlocksZ;
		Blocks.SetNum(NumBlocks);
		BlockLocks.SetNum(NumBlocks);
		for (int32 k = 0; k < NumBlocks; ++k)
		{
			Blocks[k] = TUniquePtr<BlockType>();
		}

		BlockDimensions = FVector3i(BlocksX, BlocksY, BlocksZ);
		Dimensions = FVector3i(DimX, DimY, DimZ);
	}

	/**
	 * @return the grid value at (X,Y,Z)
	 */
	ElemType GetValue(int32 X, int32 Y, int32 Z) const
	{
		return ReadValue(X, Y, Z);
	}

	/**
	 * @return the grid value at (X,Y,Z), with internal locking, so it is safe to call this from multiple read & write threads
	 */
	ElemType GetValueThreadSafe(int32 X0, int32 Y, int32 Z)
	{
		return ReadValueThreadSafe(X0, Y, Z);
	}

	/**
	 * Set the grid value at (X,Y,Z)
	 */
	void SetValue(int32 X, int32 Y, int32 Z, ElemType NewValue)
	{
		WriteValue(X, Y, Z, [NewValue](ElemType& GridValueInOut) { GridValueInOut = NewValue; });
	}

	/**
	 * Set the grid value at (X,Y,Z), with internal locking, so it is safe to call this from multiple read & write threads
	 */
	void SetValueThreadSafe(int32 X, int32 Y, int32 Z, ElemType NewValue)
	{
		WriteValueThreadSafe(X, Y, Z, [NewValue](ElemType& GridValueInOut) { GridValueInOut = NewValue; });
	}

	/**
	 * Call an external lambda with a reference to the grid value at (X,Y,Z).
	 * Called as Func(ElemType&), so the caller can both read and write the grid cell
	 */
	template<typename ProcessFunc>
	void ProcessValueThreadSafe(int32 X, int32 Y, int32 Z, ProcessFunc Func)
	{
		WriteValueThreadSafe(X, Y, Z, Func);
	}

	FAxisAlignedBox3i Bounds() const
	{
		return FAxisAlignedBox3i({0, 0, 0},{Dimensions.X, Dimensions.Y, Dimensions.Z});
	}
	FAxisAlignedBox3i BoundsInclusive() const
	{
		return FAxisAlignedBox3i({0, 0, 0},{Dimensions.X-1, Dimensions.Y-1, Dimensions.Z-1});
	}

	FVector3i ToIndex(int64 LinearIndex) const
	{
		int32 x = (int32)(LinearIndex % (int64)Dimensions.X);
		int32 y = (int32)((LinearIndex / (int64)Dimensions.X) % (int64)Dimensions.Y);
		int32 z = (int32)(LinearIndex / ((int64)Dimensions.X * (int64)Dimensions.Y));
		return FVector3i(x, y, z);
	}
	int64 ToLinear(int32 X, int32 Y, int32 Z) const
	{
		return (int64)X + (int64)Dimensions.X * ((int64)Y + (int64)Dimensions.Y * (int64)Z);
	}
	int64 ToLinear(const FVector3i& IJK) const
	{
		return (int64)IJK.X + (int64)Dimensions.X * ((int64)IJK.Y + (int64)Dimensions.Y * (int64)IJK.Z);
	}
};


typedef TBlockedDenseGrid3<float> FBlockedDenseGrid3f;
typedef TBlockedDenseGrid3<double> FBlockedDenseGrid3d;
typedef TBlockedDenseGrid3<int> FBlockedDenseGrid3i;


} // end namespace UE::Geometry
} // end namespace UE
