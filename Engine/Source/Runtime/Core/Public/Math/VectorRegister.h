// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/UnrealMathUtility.h"

// If defined, allow double->float conversion in some VectorStore functions.
#define SUPPORT_DOUBLE_TO_FLOAT_VECTOR_CONVERSION 1

// Platform specific vector intrinsics include.
#if WITH_DIRECTXMATH
#include "Math/UnrealMathDirectX.h"
#elif PLATFORM_ENABLE_VECTORINTRINSICS_NEON
#include "Math/UnrealMathNeon.h"
#elif defined(__cplusplus_cli) && !PLATFORM_HOLOLENS
#include "Math/UnrealMathFPU.h" // there are compile issues with UnrealMathSSE in managed mode, so use the FPU version
#elif PLATFORM_ENABLE_VECTORINTRINSICS
#include "Math/UnrealMathSSE.h"
#else
#include "Math/UnrealMathFPU.h"
#endif

#define SIMD_ALIGNMENT (alignof(VectorRegister))

// 'Cross-platform' vector intrinsics (built on the platform-specific ones defined above)
#include "Math/UnrealMathVectorCommon.h"

/** Vector that represents (1/255,1/255,1/255,1/255) */
extern CORE_API const VectorRegister VECTOR_INV_255;

// Old names for comparison functions, kept for compatibility.
#define VectorMask_LT( Vec1, Vec2 )			VectorCompareLT(Vec1, Vec2)
#define VectorMask_LE( Vec1, Vec2 )			VectorCompareLE(Vec1, Vec2)
#define VectorMask_GT( Vec1, Vec2 )			VectorCompareGT(Vec1, Vec2)
#define VectorMask_GE( Vec1, Vec2 )			VectorCompareGE(Vec1, Vec2)
#define VectorMask_EQ( Vec1, Vec2 )			VectorCompareEQ(Vec1, Vec2)
#define VectorMask_NE( Vec1, Vec2 )			VectorCompareNE(Vec1, Vec2)

/**
* Below this weight threshold, animations won't be blended in.
*/
#define ZERO_ANIMWEIGHT_THRESH (0.00001f)

namespace GlobalVectorConstants
{
	static const VectorRegister AnimWeightThreshold = MakeVectorRegister(ZERO_ANIMWEIGHT_THRESH, ZERO_ANIMWEIGHT_THRESH, ZERO_ANIMWEIGHT_THRESH, ZERO_ANIMWEIGHT_THRESH);
	static const VectorRegister RotationSignificantThreshold = MakeVectorRegister(1.0f - DELTA*DELTA, 1.0f - DELTA*DELTA, 1.0f - DELTA*DELTA, 1.0f - DELTA*DELTA);
}

