// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "NNXShaderParameters.h"
#include "NNXTypes.h"

enum class EGemmCScalar : uint8
{
	No = 0,
	Yes,
	NoBias,
	MAX
};

enum class EGemmAlgorithm : uint8
{
	Simple8x8 = 0,
	Simple16x16,
	Simple32x32,
	Simple256x1,
	SharedMemory8x8,
	SharedMemory16x16,
	SharedMemory32x32,
	MultiWrite1x16,
	MultiWrite2x16,
	MultiWrite1x32,
	MultiWrite2x32,
	MultiWrite4x32,
	MultiWrite2x64,
	MultiWrite4x64,
	MultiWrite8x64,
	MAX
};

class NNXHLSLSHADERS_API FMLGemmCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLGemmCS);
	SHADER_USE_PARAMETER_STRUCT(FMLGemmCS, FGlobalShader)

	class FGemmCScalar : SHADER_PERMUTATION_ENUM_CLASS("C_SCALAR", EGemmCScalar);
	class FGemmAlgorithm : SHADER_PERMUTATION_ENUM_CLASS("ALGORITHM", EGemmAlgorithm);
	class FGemmNumStackDimensions : SHADER_PERMUTATION_RANGE_INT("NUM_STACK_DIMENSIONS", 0, NNXRT_GEMM_MAX_NUM_STACK_DIMENSIONS);
	using FPermutationDomain = TShaderPermutationDomain<FGemmCScalar, FGemmAlgorithm, FGemmNumStackDimensions>;

public:
	NNXRT_GEMM_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

	static void FillInParameters(float Alpha, float Beta, int32 TransA, int32 TransB, const NNX::FMLTensorDesc &InputA, const NNX::FMLTensorDesc &InputB,
		const NNX::FMLTensorDesc &InputC, float CScalar, FMLGemmCS::FParameters& Parameters);

	static uint32 GetShapeSize(TArray<uint32> Shape);
	static uint32 GetMatMulOutputSize(TArray<uint32> ShapeA, TArray<uint32> ShapeB);

	static void FillInParametersMatMul(TArray<uint32> ShapeA, TArray<uint32> ShapeB, FMLGemmCS::FParameters& Parameters);

	static FIntVector GetGroupCount(const FMLGemmCS::FParameters& Parameters, EGemmAlgorithm Algorithm, int32 NumStackDimensions);
	static EGemmAlgorithm GetAlgorithm(const FMLGemmCS::FParameters& Parameters);
};
