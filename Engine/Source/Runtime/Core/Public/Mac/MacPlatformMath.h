// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================================
	MacPlatformMath.h: Mac platform Math functions
==============================================================================================*/

#pragma once
#include "Clang/ClangPlatformMath.h"
#include "Mac/MacSystemIncludes.h"
#include "Math/UnrealPlatformMathSSE.h"

/**
* Mac implementation of the Math OS functions
**/
struct FMacPlatformMath : public FClangPlatformMath
{
	static FORCEINLINE int32 TruncToInt(float F)
	{
		return UnrealPlatformMathSSE::TruncToInt(F);
	}

	static FORCEINLINE float TruncToFloat(float F)
	{
		return UnrealPlatformMathSSE::TruncToFloat(F);
	}

	static FORCEINLINE double TruncToDouble(double F)
	{
		return UnrealPlatformMathSSE::TruncToDouble(F);
	}

	static FORCEINLINE int32 RoundToInt(float F)
	{
		return UnrealPlatformMathSSE::RoundToInt(F);
	}

	static FORCEINLINE float RoundToFloat(float F)
	{
		return UnrealPlatformMathSSE::RoundToFloat(F);
	}

	static FORCEINLINE double RoundToDouble(double F)
	{
		return UnrealPlatformMathSSE::RoundToDouble(F);
	}

	static FORCEINLINE int32 FloorToInt(float F)
	{
		return UnrealPlatformMathSSE::FloorToInt(F);
	}

	static FORCEINLINE float FloorToFloat(float F)
	{
		return UnrealPlatformMathSSE::FloorToFloat(F);
	}

	static FORCEINLINE double FloorToDouble(double F)
	{
		return UnrealPlatformMathSSE::FloorToDouble(F);
	}

	static FORCEINLINE int32 CeilToInt(float F)
	{
		return UnrealPlatformMathSSE::CeilToInt(F);
	}

	static FORCEINLINE float CeilToFloat(float F)
	{
		return UnrealPlatformMathSSE::CeilToFloat(F);
	}

	static FORCEINLINE double CeilToDouble(double F)
	{
		return UnrealPlatformMathSSE::CeilToDouble(F);
	}

#if PLATFORM_ENABLE_POPCNT_INTRINSIC
	/**
	 * Use the SSE instruction to count bits
	 */
	static FORCEINLINE int32 CountBits(uint64 Bits)
	{
		return __builtin_popcountll(Bits);
	}
#endif

	static FORCEINLINE bool IsNaN( float A ) { return isnan(A) != 0; }
	static FORCEINLINE bool IsFinite( float A ) { return isfinite(A); }

#if PLATFORM_ENABLE_VECTORINTRINSICS
	static FORCEINLINE float InvSqrt(float F)
	{
		return UnrealPlatformMathSSE::InvSqrt(F);
	}

	static FORCEINLINE float InvSqrtEst( float F )
	{
		return UnrealPlatformMathSSE::InvSqrtEst(F);
	}
#endif
};

typedef FMacPlatformMath FPlatformMath;

