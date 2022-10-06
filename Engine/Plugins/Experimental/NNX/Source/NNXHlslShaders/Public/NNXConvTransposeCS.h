// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "NNXShaderParameters.h"
#include "NNXTypes.h"

enum class EConvTransposeAlgorithm : uint8
{
	SharedMemory = 0,
	MAX
};

enum class EConvTransposeGroupSize : uint8
{
	Size128 = 0,
	Size256,
	Size512,
	MAX
};

enum class EConvTransposeAutoPad : uint8
{
	NOTSET = 0,// Use pad values passed in the array
	SAME_UPPER,// Auto-pad to match input and output shape with potetnial extra padding at the end
	SAME_LOWER,// Auto-pad to match input and output shape with potetnial extra padding at the beginning
	VALID,// Set all paddings to zero
	MAX
};

class NNXHLSLSHADERS_API FMLConvTransposeCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLConvTransposeCS);
	SHADER_USE_PARAMETER_STRUCT(FMLConvTransposeCS, FGlobalShader);

	class FConvTransposeAlgorithm : SHADER_PERMUTATION_ENUM_CLASS("ALGORITHM", EConvTransposeAlgorithm);
	class FConvTransposeGroupSize : SHADER_PERMUTATION_ENUM_CLASS("GROUP_SIZE", EConvTransposeGroupSize);
	class FConvTransposeNumStackDimensions : SHADER_PERMUTATION_RANGE_INT("NUM_STACK_DIMENSIONS", 1, NNXRT_CONVTRANSPOSE_MAX_NUM_STACK_DIMENSIONS);
	class FConvTransposeNumReadsPerThread : SHADER_PERMUTATION_RANGE_INT("NUM_READS_PER_THREAD_POW2", NNXRT_CONVTRANSPOSE_MIN_NUM_READS_PER_THREAD_POW2, NNXRT_CONVTRANSPOSE_MAX_NUM_READS_PER_THREAD_POW2);
	class FConvTransposeHasB : SHADER_PERMUTATION_BOOL("HAS_B");
	using FPermutationDomain = TShaderPermutationDomain<FConvTransposeAlgorithm, FConvTransposeGroupSize, FConvTransposeNumStackDimensions, FConvTransposeNumReadsPerThread, FConvTransposeHasB>;

public:
	NNXRT_CONVTRANSPOSE_PARAMETER_STRUCT();

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

	static TArray<int32> GetOutputShape(TArray<int32> XShape, TArray<int32> WShape, EConvTransposeAutoPad AutoPad, TArray<int32> Dilations, TArray<int32> Strides, TArray<int32> Pads, TArray<int32> OutputPadding, int32 Group);

	static void FillInParameters(EConvTransposeGroupSize GroupSize, TArray<int32> XShape, TArray<int32> WShape, bool HasB, EConvTransposeAutoPad AutoPad, int32 Group, TArray<int32> Dilations, TArray<int32> Strides, TArray<int32> Pads, TArray<int32> OutputPadding, FMLConvTransposeCS::FParameters& Parameters);

	static int32 GetNumReadsPerThread(EConvTransposeGroupSize GroupSize, TArray<int32> WShape, TArray<int32> Dilations, TArray<int32> Strides);

	static TArray<int32> GetGroupShape(EConvTransposeGroupSize GroupSize, int32 NumDimensions);

	static FIntVector GetGroupCount(TArray<int32> YShape, TArray<int32> GroupShape);

	static EConvTransposeGroupSize GetMinimalGroupSize(TArray<int32> WShape);

private:
	static TArray<int32> GetXBlockShape(TArray<int32> GroupShape, TArray<int32> WShape, TArray<int32> Dilations, TArray<int32> Strides);

	static TArray<int32> GetPadding(TArray<int32> WShape, EConvTransposeAutoPad AutoPad, TArray<int32> Dilations, TArray<int32> Strides, TArray<int32> Pads, TArray<int32> OutputPadding);

	static int32 GetNumThreadsPerGroup(EConvTransposeGroupSize GroupSize);

	static TArray<int32> GetGridShape(TArray<int32> YShape, TArray<int32> GroupShape);
};