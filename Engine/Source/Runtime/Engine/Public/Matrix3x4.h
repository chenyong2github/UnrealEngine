// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ShaderParameterMacros.h"

/**
* 3x4 matrix of floating point values.
*/

MS_ALIGN(16) struct FMatrix3x4
{
	float M[3][4];

	FORCEINLINE void SetMatrix(const FMatrix& Mat)
	{
		const FMatrix::FReal* RESTRICT Src = &(Mat.M[0][0]);
		float* RESTRICT Dest = &(M[0][0]);

		Dest[0] = (FMatrix::FReal)Src[0];   // [0][0]
		Dest[1] = (FMatrix::FReal)Src[1];   // [0][1]
		Dest[2] = (FMatrix::FReal)Src[2];   // [0][2]
		Dest[3] = (FMatrix::FReal)Src[3];   // [0][3]

		Dest[4] = (FMatrix::FReal)Src[4];   // [1][0]
		Dest[5] = (FMatrix::FReal)Src[5];   // [1][1]
		Dest[6] = (FMatrix::FReal)Src[6];   // [1][2]
		Dest[7] = (FMatrix::FReal)Src[7];   // [1][3]

		Dest[8] = (FMatrix::FReal)Src[8];   // [2][0]
		Dest[9] = (FMatrix::FReal)Src[9];   // [2][1]
		Dest[10] = (FMatrix::FReal)Src[10]; // [2][2]
		Dest[11] = (FMatrix::FReal)Src[11]; // [2][3]
	}

	FORCEINLINE void SetMatrixTranspose(const FMatrix& Mat)
	{
		const FMatrix::FReal* RESTRICT Src = &(Mat.M[0][0]);
		float* RESTRICT Dest = &(M[0][0]);

		Dest[0] = (FMatrix::FReal)Src[0];   // [0][0]
		Dest[1] = (FMatrix::FReal)Src[4];   // [1][0]
		Dest[2] = (FMatrix::FReal)Src[8];   // [2][0]
		Dest[3] = (FMatrix::FReal)Src[12];  // [3][0]

		Dest[4] = (FMatrix::FReal)Src[1];   // [0][1]
		Dest[5] = (FMatrix::FReal)Src[5];   // [1][1]
		Dest[6] = (FMatrix::FReal)Src[9];   // [2][1]
		Dest[7] = (FMatrix::FReal)Src[13];  // [3][1]

		Dest[8] = (FMatrix::FReal)Src[2];   // [0][2]
		Dest[9] = (FMatrix::FReal)Src[6];   // [1][2]
		Dest[10] = (FMatrix::FReal)Src[10]; // [2][2]
		Dest[11] = (FMatrix::FReal)Src[14]; // [3][2]
	}
} GCC_ALIGN(16);

template<>
struct TShaderParameterTypeInfo<FMatrix3x4>
{
	static constexpr EUniformBufferBaseType BaseType = UBMT_FLOAT32;
	static constexpr int32 NumRows = 3;
	static constexpr int32 NumColumns = 4;
	static constexpr int32 NumElements = 0;
	static constexpr int32 Alignment = 16;
	static constexpr bool bIsStoredInConstantBuffer = true;

	using TAlignedType = TAlignedTypedef<FMatrix3x4, Alignment>::Type;

	static const FShaderParametersMetadata* GetStructMetadata() { return NULL; }
};