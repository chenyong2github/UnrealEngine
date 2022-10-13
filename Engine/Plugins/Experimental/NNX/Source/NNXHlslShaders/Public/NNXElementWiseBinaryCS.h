// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RenderGraphUtils.h"
#include "NNXOperator.h"

class FElementWiseBinaryConstants
{
public:
	static const int32 MAX_NUM_DIMENSIONS{8};
};

//
//
//
class NNXHLSLSHADERS_API FMLElementWiseBinaryCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLElementWiseBinaryCS);
	SHADER_USE_PARAMETER_STRUCT(FMLElementWiseBinaryCS, FGlobalShader)

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, LHSInput) \
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, RHSInput) \
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, Output) \
		SHADER_PARAMETER_ARRAY(FUintVector4, TensorInfo, [FElementWiseBinaryConstants::MAX_NUM_DIMENSIONS]) \
		SHADER_PARAMETER(uint32, Num) \
		SHADER_PARAMETER(uint32, ThreadCountX) \
	END_SHADER_PARAMETER_STRUCT()
	
	static const uint32 THREADGROUP_SIZE_X;

	class FOperatorType : SHADER_PERMUTATION_ENUM_CLASS("OP_TYPENAME", EMLElementWiseBinaryOperatorType);
	class FBinaryNumDimensions : SHADER_PERMUTATION_RANGE_INT("NUM_DIMENSIONS", 1, FElementWiseBinaryConstants::MAX_NUM_DIMENSIONS);
	using FPermutationDomain = TShaderPermutationDomain<FOperatorType, FBinaryNumDimensions>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

private:

	static const FString GetOpFunc(EMLElementWiseBinaryOperatorType);
};
