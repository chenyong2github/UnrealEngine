// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/IntVector.h"
#include <limits>


namespace IndexConstants
{
	constexpr int InvalidID = -1;
}

/**
 * 2-index tuple. Ported from g3Sharp library, with the intention of
 * maintaining compatibility with existing g3Sharp code. Has an API
 * similar to WildMagic, GTEngine, Eigen, etc.
 */
struct FIndex2i
{
	int A{ IndexConstants::InvalidID };
	int B{ IndexConstants::InvalidID };

	constexpr FIndex2i() = default;
	constexpr FIndex2i(int ValA, int ValB)
		: A(ValA), B(ValB)
	{}

	constexpr static FIndex2i Zero()
	{
		return FIndex2i(0, 0);
	}
	constexpr static FIndex2i Max()
	{
		return FIndex2i(TNumericLimits<int>::Max(), TNumericLimits<int>::Max());
	}
	constexpr static FIndex2i Invalid()
	{
		return FIndex2i(IndexConstants::InvalidID, IndexConstants::InvalidID);
	}

	int& operator[](int Idx)
	{
		return (&A)[Idx];
	}
	const int& operator[](int Idx) const
	{
		return (&A)[Idx];
	}

	inline bool operator==(const FIndex2i& Other) const
	{
		return A == Other.A && B == Other.B;
	}

	inline bool operator!=(const FIndex2i& Other) const
	{
		return A != Other.A || B != Other.B;
	}

	int IndexOf(int Value) const
	{
		return (A == Value) ? 0 : ((B == Value) ? 1 : -1);
	}

	bool Contains(int Value) const
	{
		return (A == Value) || (B == Value);
	}

	inline void Swap()
	{
		int tmp = A;
		A = B;
		B = tmp;
	}
};

FORCEINLINE uint32 GetTypeHash(const FIndex2i& Index)
{
	// (this is how FIntVector and all the other FVectors do their hash functions)
	// Note: this assumes there's no padding that could contain non compared data.
	return FCrc::MemCrc_DEPRECATED(&Index, sizeof(FIndex2i));
}



/**
 * 3-index tuple. Ported from g3Sharp library, with the intention of
 * maintaining compatibility with existing g3Sharp code. Has an API
 * similar to WildMagic, GTEngine, Eigen, etc.
 *
 * Implicit casts to/from FIntVector are defined.
 */
struct FIndex3i
{
	int A{ IndexConstants::InvalidID };
	int B{ IndexConstants::InvalidID };
	int C{ IndexConstants::InvalidID };

	constexpr FIndex3i() = default;
	constexpr FIndex3i(int ValA, int ValB, int ValC)
		: A(ValA), B(ValB), C(ValC)
	{}

	constexpr static FIndex3i Zero()
	{
		return FIndex3i(0, 0, 0);
	}
	constexpr static FIndex3i Max()
	{
		return FIndex3i(TNumericLimits<int>::Max(), TNumericLimits<int>::Max(), TNumericLimits<int>::Max());
	}
	constexpr static FIndex3i Invalid()
	{
		return FIndex3i(IndexConstants::InvalidID, IndexConstants::InvalidID, IndexConstants::InvalidID);
	}

	int& operator[](int Idx)
	{
		return (&A)[Idx];
	}
	const int& operator[](int Idx) const
	{
		return (&A)[Idx];
	}

	bool operator==(const FIndex3i& Other) const
	{
		return A == Other.A && B == Other.B && C == Other.C;
	}

	bool operator!=(const FIndex3i& Other) const
	{
		return A != Other.A || B != Other.B || C != Other.C;
	}

	int IndexOf(int Value) const
	{
		return (A == Value) ? 0 : ((B == Value) ? 1 : (C == Value ? 2 : -1));
	}

	bool Contains(int Value) const
	{
		return (A == Value) || (B == Value) || (C == Value);
	}

	operator FIntVector() const
	{
		return FIntVector(A, B, C);
	}
	FIndex3i(const FIntVector& Vec)
	{
		A = Vec.X;
		B = Vec.Y;
		C = Vec.Z;
	}
};

FORCEINLINE uint32 GetTypeHash(const FIndex3i& Index)
{
	// (this is how FIntVector and all the other FVectors do their hash functions)
	// Note: this assumes there's no padding that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Index, sizeof(FIndex3i));
}



/**
 * 4-index tuple. Ported from g3Sharp library, with the intention of
 * maintaining compatibility with existing g3Sharp code. Has an API
 * similar to WildMagic, GTEngine, Eigen, etc.
 */

struct FIndex4i
{
	int A, B, C, D;

	FIndex4i()
	{
	}
	FIndex4i(int ValA, int ValB, int ValC, int ValD)
	{
		this->A = ValA;
		this->B = ValB;
		this->C = ValC;
		this->D = ValD;
	}

	static FIndex4i Zero()
	{
		return FIndex4i(0, 0, 0, 0);
	}
	static FIndex4i Max()
	{
		return FIndex4i(TNumericLimits<int>::Max(), TNumericLimits<int>::Max(), TNumericLimits<int>::Max(), TNumericLimits<int>::Max());
	}
	static FIndex4i Invalid()
	{
		return FIndex4i(IndexConstants::InvalidID, IndexConstants::InvalidID, IndexConstants::InvalidID, IndexConstants::InvalidID);
	}

	int& operator[](int Idx)
	{
		return (&A)[Idx];
	}
	const int& operator[](int Idx) const
	{
		return (&A)[Idx];
	}

	bool operator==(const FIndex4i& Other) const
	{
		return A == Other.A && B == Other.B && C == Other.C && D == Other.D;
	}

	bool operator!=(const FIndex4i& Other) const
	{
		return A != Other.A || B != Other.B || C != Other.C || D != Other.D;
	}

	bool Contains(int Idx) const
	{
		return A == Idx || B == Idx || C == Idx || D == Idx;
	}
};

FORCEINLINE uint32 GetTypeHash(const FIndex4i& Index)
{
	// (this is how FIntVector and all the other FVectors do their hash functions)
	// Note: this assumes there's no padding that could contain uncompared data.
	return FCrc::MemCrc_DEPRECATED(&Index, sizeof(FIndex4i));
}
