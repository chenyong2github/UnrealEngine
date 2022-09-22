// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "NNXShaderParameters.h"

enum class EConvAlgorithm : uint8
{
	SharedMemory = 0,
	MAX
};

enum class EConvGroupSize : uint8
{
	Size128 = 0,
	Size256,
	Size512,
	MAX
};

enum class EConvAutoPad : uint8
{
	NOTSET = 0,// Use pad values passed in the array
	SAME_UPPER,// Auto-pad to match input and output shape with potetnial extra padding at the end
	SAME_LOWER,// Auto-pad to match input and output shape with potetnial extra padding at the beginning
	VALID,// Set all paddings to zero
	MAX
};

class FConvConstants
{
public:
	static const int32 MIN_NUM_READS_PER_THREAD_POW2{1};
	static const int32 MAX_NUM_READS_PER_THREAD_POW2{3};
};

class NNXHLSLSHADERS_API FMLConvCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLConvCS);
	SHADER_USE_PARAMETER_STRUCT(FMLConvCS, FGlobalShader)

	class FConvAlgorithm : SHADER_PERMUTATION_ENUM_CLASS("ALGORITHM", EConvAlgorithm);
	class FConvGroupSize : SHADER_PERMUTATION_ENUM_CLASS("GROUP_SIZE", EConvGroupSize);
	class FConvNumDimensions : SHADER_PERMUTATION_RANGE_INT("NUM_DIMENSIONS", 1, NNXRT_CONV_MAX_NUM_DIMENSIONS);
	class FConvNumReadsPerThread : SHADER_PERMUTATION_RANGE_INT("NUM_READS_PER_THREAD_POW2", FConvConstants::MIN_NUM_READS_PER_THREAD_POW2, FConvConstants::MAX_NUM_READS_PER_THREAD_POW2);
	class FConvHasB : SHADER_PERMUTATION_BOOL("HAS_B");
	using FPermutationDomain = TShaderPermutationDomain<FConvAlgorithm, FConvGroupSize, FConvNumDimensions, FConvNumReadsPerThread, FConvHasB>;

public:
	NNXRT_CONV_PARAMETER_STRUCT();

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

public:
	static TArray<int32> GetOutputShape(TArray<int32> XShape, TArray<int32> WShape, EConvAutoPad AutoPad, TArray<int32> Dilations, TArray<int32> Strides, TArray<int32> Pads);

	static void FillInParameters(EConvGroupSize GroupSize, TArray<int32> XShape, TArray<int32> WShape, bool HasB, EConvAutoPad AutoPad, int Group, TArray<int32> Dilations, TArray<int32> Strides, TArray<int32> Pads, FMLConvCS::FParameters& Parameters);

	static int32 GetNumReadsPerThread(EConvGroupSize GroupSize, TArray<int32> WShape, TArray<int32> Dilations, TArray<int32> Strides);

	/**
	 * @brief Computes the group shape such that all dimension have roughly equal sizes.
	 *
	 * @param GroupSize The enum indicating the number of threads contained by a single group.
	 * @param NumDimensions The number of dimensions.
	 * @return TArray<int32> An array of size \p NumDimensions containing the number of threads in each dimension to form a volume of a total number of threads indicated by \p GroupSize
	 */
	static TArray<int32> GetGroupShape(EConvGroupSize GroupSize, int32 NumDimensions);

	/**
	 * @brief Get the group count vector used to launch the gpu shader thread groups
	 *
	 * @param YShape The shape of the output as computed by GetOutputShape()
	 * @param YShape The shape of a single thread group as computed by GetGroupShape()
	 * @return FIntVector The number of thread groups to instantiate. z corresponds to the batch and y to the output kernel.
	 */
	static FIntVector GetGroupCount(TArray<int32> YShape, TArray<int32> GroupShape);

	static EConvGroupSize GetMinimalGroupSize(TArray<int32> WShape);
};