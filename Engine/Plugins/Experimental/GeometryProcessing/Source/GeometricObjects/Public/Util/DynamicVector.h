// Copyright Epic Games, Inc. All Rights Reserved.

// Port of geometry3cpp dvector

#pragma once

#include <CoreMinimal.h>
#include "Containers/StaticArray.h"
#include "Containers/IndirectArray.h"
#include "VectorTypes.h"
#include "IndexTypes.h"


/*
 * Blocked array with fixed, power-of-two sized blocks.
 *
 * Iterator functions suitable for use with range-based for are provided
 */
template <class Type>
class TDynamicVector
{
public:
	TDynamicVector()
	{
		Blocks.Add(new BlockType());
	}

	TDynamicVector(const TDynamicVector& Copy) = default;
	TDynamicVector(TDynamicVector&& Moved)
	{
		Blocks = MoveTemp(Moved.Blocks);
		CurBlock = MoveTemp(Moved.CurBlock);
		CurBlockUsed = MoveTemp(Moved.CurBlockUsed);
		Moved.Blocks.Add(new BlockType());
		Moved.CurBlock = 0;
		Moved.CurBlockUsed = 0;
	}
	TDynamicVector& operator=(const TDynamicVector& Copy) = default;
	TDynamicVector& operator=(TDynamicVector&& Moved)
	{
		Blocks = MoveTemp(Moved.Blocks);
		CurBlock = MoveTemp(Moved.CurBlock);
		CurBlockUsed = MoveTemp(Moved.CurBlockUsed);
		Moved.Blocks.Add(new BlockType());
		Moved.CurBlock = 0;
		Moved.CurBlockUsed = 0;
		return *this;
	}

	TDynamicVector(const TArray<Type>& Array)
	{
		SetNum(Array.Num());
		for (int Idx = 0; Idx < Array.Num(); Idx++)
		{
			(*this)[Idx] = Array[Idx];
		}
	}
	TDynamicVector(TArrayView<const Type> Array)
	{
		SetNum(Array.Num());
		for (int Idx = 0; Idx < Array.Num(); Idx++)
		{
			(*this)[Idx] = Array[Idx];
		}
	}

	inline void Clear();
	inline void Fill(const Type& Value);
	inline void Resize(size_t Count);
	inline void Resize(size_t Count, const Type& InitValue);
	inline void SetNum(size_t Count) { Resize(Count); }

	inline bool IsEmpty() const { return CurBlock == 0 && CurBlockUsed == 0; }
	inline size_t GetLength() const { return CurBlock * BlockSize + CurBlockUsed; }
	inline size_t Num() const { return CurBlock * BlockSize + CurBlockUsed; }
	inline int GetBlockSize() const { return BlockSize; }
	inline size_t GetByteCount() const { return Blocks.Num() * BlockSize * sizeof(Type); }

	inline void Add(const Type& Data);
	inline void Add(const TDynamicVector& Data);
	inline void PopBack();

	inline void InsertAt(const Type& Data, unsigned int Index);

	inline const Type& Front() const { return Blocks[0][0]; }
	inline const Type& Back() const { return Blocks[CurBlock][CurBlockUsed - 1]; }

	inline Type& operator[](unsigned int Index)
	{
		return Blocks[Index >> nShiftBits][Index & BlockIndexBitmask];
	}
	inline const Type& operator[](unsigned int Index) const
	{
		return Blocks[Index >> nShiftBits][Index & BlockIndexBitmask];
	}

	// apply f() to each member sequentially
	template <typename Func>
	void Apply(const Func& f);

public:
	/*
	 * FIterator class iterates over values of vector
	 */
	class FIterator
	{
	public:
		inline const Type& operator*() const
		{
			return (*DVector)[Idx];
		}
		inline Type& operator*()
		{
			return (*DVector)[Idx];
		}
		inline FIterator& operator++()   // prefix
		{
			Idx++;
			return *this;
		}
		inline FIterator operator++(int) // postfix
		{
			FIterator Copy(*this);
			Idx++;
			return Copy;
		}
		inline bool operator==(const FIterator& Itr2)
		{
			return DVector == Itr2.DVector && Idx == Itr2.Idx;
		}
		inline bool operator!=(const FIterator& Itr2)
		{
			return DVector != Itr2.DVector || Idx != Itr2.Idx;
		}

	private:
		friend class TDynamicVector;
		FIterator(TDynamicVector* DVectorIn, int IdxIn)
			: DVector(DVectorIn), Idx(IdxIn){}
		TDynamicVector* DVector{};
		int Idx{0};
	};

	/** @return iterator at beginning of vector */
	FIterator begin()
	{
		return IsEmpty() ? end() : FIterator{this};
	}
	/** @return iterator at end of vector */
	FIterator end()
	{
		return FIterator{this, (int)GetLength()};
	}

	/*
	 * FConstIterator class iterates over values of vector
	 */
	class FConstIterator
	{
	public:
		inline const Type& operator*() const
		{
			return (*DVector)[Idx];
		}
		inline FConstIterator& operator++()   // prefix
		{
			Idx++;
			return *this;
		}
		inline FConstIterator operator++(int) // postfix
		{
			FConstIterator Copy(*this);
			Idx++;
			return Copy;
		}
		inline bool operator==(const FConstIterator& Itr2)
		{
			return DVector == Itr2.DVector && Idx == Itr2.Idx;
		}
		inline bool operator!=(const FConstIterator& Itr2)
		{
			return DVector != Itr2.DVector || Idx != Itr2.Idx;
		}

	private:
		friend class TDynamicVector;
		FConstIterator(const TDynamicVector* DVectorIn, int IdxIn)
			: DVector(DVectorIn), Idx(IdxIn){}
		const TDynamicVector* DVector{};
		int Idx{0};
	};

	/** @return iterator at beginning of vector */
	FConstIterator begin() const
	{
		return IsEmpty() ? end() : FConstIterator{this, 0};
	}
	/** @return iterator at end of vector */
	FConstIterator end() const
	{
		return FConstIterator{this, (int)GetLength()};
	}

private:
	static constexpr int nShiftBits = 11;
	static constexpr int BlockSize = 1 << nShiftBits;
	static constexpr int BlockIndexBitmask = BlockSize - 1; // low 11 bits
	static_assert( BlockSize && ((BlockSize & (BlockSize - 1)) == 0), "DynamicVector: BlockSize must be a power of two");

	unsigned int CurBlock{0};
	unsigned int CurBlockUsed{0};

	using BlockType = TStaticArray<Type, BlockSize>;
	TIndirectArray<BlockType> Blocks;
};

template <class Type, int N>
class TDynamicVectorN
{
public:
	TDynamicVectorN() = default;
	TDynamicVectorN(const TDynamicVectorN& Copy) = default;
	TDynamicVectorN(TDynamicVectorN&& Moved) = default;
	TDynamicVectorN& operator=(const TDynamicVectorN& Copy) = default;
	TDynamicVectorN& operator=(TDynamicVectorN&& Moved) = default;

	inline void Clear()
	{
		Data.Clear();
	}
	inline void Fill(const Type& Value)
	{
		Data.Fill(Value);
	}
	inline void Resize(size_t Count)
	{
		Data.Resize(Count * N);
	}
	inline void Resize(size_t Count, const Type& InitValue)
	{
		Data.Resize(Count * N, InitValue);
	}
	inline bool IsEmpty() const
	{
		return Data.IsEmpty();
	}
	inline size_t GetLength() const
	{
		return Data.GetLength() / N;
	}
	inline int GetBlockSize() const
	{
		return Data.GetBlockSize();
	}
	inline size_t GetByteCount() const
	{
		return Data.GetByteCount();
	}

	// simple struct to help pass N-dimensional data without presuming a vector type (e.g. just via initializer list)
	struct ElementVectorN
	{
		Type Data[N];
	};

	inline void Add(const ElementVectorN& AddData)
	{
		// todo specialize for N=2,3,4
		for (int i = 0; i < N; i++)
		{
			Data.Add(AddData.Data[i]);
		}
	}

	inline void PopBack()
	{
		for (int i = 0; i < N; i++)
		{
			PopBack();
		}
	} // TODO specialize

	inline void InsertAt(const ElementVectorN& AddData, unsigned int Index)
	{
		for (int i = 1; i <= N; i++)
		{
			Data.InsertAt(AddData.Data[N - i], N * (Index + 1) - i);
		}
	}

	inline Type& operator()(unsigned int TopIndex, unsigned int SubIndex)
	{
		return Data[TopIndex * N + SubIndex];
	}
	inline const Type& operator()(unsigned int TopIndex, unsigned int SubIndex) const
	{
		return Data[TopIndex * N + SubIndex];
	}
	inline void SetVector2(unsigned int TopIndex, const FVector2<Type>& V)
	{
		check(N >= 2);
		unsigned int i = TopIndex * N;
		Data[i] = V.X;
		Data[i + 1] = V.Y;
	}
	inline void SetVector3(unsigned int TopIndex, const FVector3<Type>& V)
	{
		check(N >= 3);
		unsigned int i = TopIndex * N;
		Data[i] = V.X;
		Data[i + 1] = V.Y;
		Data[i + 2] = V.Z;
	}
	inline FVector2<Type> AsVector2(unsigned int TopIndex) const
	{
		check(N >= 2);
		return FVector2<Type>(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1]);
	}
	inline FVector3<Type> AsVector3(unsigned int TopIndex) const
	{
		check(N >= 3);
		return FVector3<Type>(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1],
			Data[TopIndex * N + 2]);
	}
	inline FIndex2i AsIndex2(unsigned int TopIndex) const
	{
		check(N >= 2);
		return FIndex2i(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1]);
	}
	inline FIndex3i AsIndex3(unsigned int TopIndex) const
	{
		check(N >= 3);
		return FIndex3i(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1],
			Data[TopIndex * N + 2]);
	}
	inline FIndex4i AsIndex4(unsigned int TopIndex) const
	{
		check(N >= 4);
		return FIndex4i(
			Data[TopIndex * N + 0],
			Data[TopIndex * N + 1],
			Data[TopIndex * N + 2],
			Data[TopIndex * N + 3]);
	}

private:
	TDynamicVector<Type> Data;

	friend class FIterator;
};

template class TDynamicVectorN<double, 2>;

using  TDynamicVector3f = TDynamicVectorN<float, 3>;
using  TDynamicVector2f = TDynamicVectorN<float, 2>;
using  TDynamicVector3d = TDynamicVectorN<double, 3>;
using  TDynamicVector2d = TDynamicVectorN<double, 2>;
using  TDynamicVector3i = TDynamicVectorN<int, 3>;
using  TDynamicVector2i = TDynamicVectorN<int, 2>;

template <class Type>
void TDynamicVector<Type>::Clear()
{
	Blocks.Empty();
	CurBlock = 0;
	CurBlockUsed = 0;
	Blocks.Add(new BlockType());
}

template <class Type>
void TDynamicVector<Type>::Fill(const Type& Value)
{
	size_t Count = Blocks.Num();
	for (unsigned int i = 0; i < Count; ++i)
	{
		for (uint32 ElementIndex = 0; ElementIndex < BlockSize; ++ElementIndex)
		{
			Blocks[i][ElementIndex] = Value;
		}
	}
}

template <class Type>
void TDynamicVector<Type>::Resize(size_t Count)
{
	if (GetLength() == Count)
	{
		return;
	}

	// figure out how many segments we need
	int nNumSegs = 1 + (int)Count / BlockSize;

	// figure out how many are currently allocated...
	int32 nCurCount = Blocks.Num();

	// resize to right number of segments
	if (nNumSegs >= nCurCount)
	{
		// allocate new segments
		for (int i = (int)nCurCount; i < nNumSegs; ++i)
		{
			Blocks.Add(new BlockType());
		}
	}
	else
	{
		Blocks.RemoveAt(nNumSegs, nCurCount - nNumSegs);
	}

	// mark last segment
	CurBlockUsed = (unsigned int)(Count - (nNumSegs - 1) * BlockSize);
	CurBlock = nNumSegs - 1;
}

template <class Type>
void TDynamicVector<Type>::Resize(size_t Count, const Type& InitValue)
{
	size_t nCurSize = GetLength();
	Resize(Count);
	for (size_t Index = nCurSize; Index < Count; ++Index)
	{
		this->operator[](Index) = InitValue;
	}
}

template <class Type>
void TDynamicVector<Type>::Add(const Type& Value)
{
	if (CurBlockUsed == BlockSize)
	{
		if (CurBlock == Blocks.Num() - 1)
		{
			Blocks.Add(new BlockType());
		}
		CurBlock++;
		CurBlockUsed = 0;
	}
	Blocks[CurBlock][CurBlockUsed] = Value;
	CurBlockUsed++;
}


template <class Type>
void TDynamicVector<Type>::Add(const TDynamicVector<Type>& AddData)
{
	// @todo it could be more efficient to use memcopies here...
	size_t nSize = AddData.Num();
	for (unsigned int k = 0; k < nSize; ++k)
	{
		Add(AddData[k]);
	}
}

template <class Type>
void TDynamicVector<Type>::PopBack()
{
	if (CurBlockUsed > 0)
	{
		CurBlockUsed--;
	}
	if (CurBlockUsed == 0 && CurBlock > 0)
	{
		CurBlock--;
		CurBlockUsed = BlockSize;
		// remove block ??
	}
}

template <class Type>
void TDynamicVector<Type>::InsertAt(const Type& AddData, unsigned int Index)
{
	size_t s = GetLength();
	if (Index == s)
	{
		Add(AddData);
	}
	else if (Index > s)
	{
		Resize(Index);
		Add(AddData);
	}
	else
	{
		(*this)[Index] = AddData;
	}
}

template <typename Type>
template <typename Func>
void TDynamicVector<Type>::Apply(const Func& applyF)
{
	for (int bi = 0; bi < CurBlock; ++bi)
	{
		auto block = Blocks[bi];
		for (int k = 0; k < BlockSize; ++k)
		{
			applyF(block[k], k);
		}
	}
	auto lastblock = Blocks[CurBlock];
	for (int k = 0; k < CurBlockUsed; ++k)
	{
		applyF(lastblock[k], k);
	}
}
