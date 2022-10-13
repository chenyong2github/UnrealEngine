// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RenderGraphUtils.h"
#include "NNXOperator.h"

class FElementWiseVariadicConstants
{
public:
	static const int32 MAX_NUM_DIMENSIONS{8};
};

class NNXHLSLSHADERS_API FMLElementWiseVariadicCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLElementWiseVariadicCS);
	SHADER_USE_PARAMETER_STRUCT(FMLElementWiseVariadicCS, FGlobalShader)

public:

	static const uint32 THREADGROUP_SIZE_X;
	static const uint32 MAX_NUM_INPUT = 4;

	class FOperatorType : SHADER_PERMUTATION_ENUM_CLASS("OP_TYPENAME", EMLElementWiseVariadicOperatorType);
	class FApplyScale : SHADER_PERMUTATION_BOOL("APPLYSCALE");
	class FOutputAsInput : SHADER_PERMUTATION_BOOL("OUTPUTASINPUT");
	class FNumInput : SHADER_PERMUTATION_RANGE_INT("NUMINPUT", 1, MAX_NUM_INPUT);
	class FVariadicNumDimensions : SHADER_PERMUTATION_RANGE_INT("NUM_DIMENSIONS", 1, FElementWiseVariadicConstants::MAX_NUM_DIMENSIONS);
	using FPermutationDomain = TShaderPermutationDomain<FOperatorType, FApplyScale, FOutputAsInput, FNumInput, FVariadicNumDimensions>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, Input0)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, Input1)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, Input2)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, Input3)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, Output)
		SHADER_PARAMETER_ARRAY(FUintVector4, InputTensorInfo, [FElementWiseVariadicConstants::MAX_NUM_DIMENSIONS])
		SHADER_PARAMETER_ARRAY(FUintVector4, OutputTensorInfo, [FElementWiseVariadicConstants::MAX_NUM_DIMENSIONS])
		SHADER_PARAMETER(uint32, Num)
		SHADER_PARAMETER(uint32, ThreadCountX)
		SHADER_PARAMETER(float, Scale)
	END_SHADER_PARAMETER_STRUCT()

private:

	static const FString GetOpFunc(EMLElementWiseVariadicOperatorType);
};
