// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "RenderGraphUtils.h"
#include "NNXOperator.h"

//
//
//
class NNXHLSLSHADERS_API FMLElementWiseCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLElementWiseCS);
	SHADER_USE_PARAMETER_STRUCT(FMLElementWiseCS, FGlobalShader)

public:

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, Input)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<float>, Output)
		SHADER_PARAMETER(uint32, Num)
		SHADER_PARAMETER(uint32, ThreadCountX)
		SHADER_PARAMETER(float, Alpha)
		SHADER_PARAMETER(float, Beta)
		SHADER_PARAMETER(float, Gamma)
	END_SHADER_PARAMETER_STRUCT()

	static const uint32 THREADGROUP_SIZE_X;

	class FOperatorType : SHADER_PERMUTATION_ENUM_CLASS("OP_TYPENAME", EMLElementWiseUnaryOperatorType);
	using FPermutationDomain = TShaderPermutationDomain<FOperatorType>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

	

private:

	static const FString GetOpFunc(EMLElementWiseUnaryOperatorType OpType);
};
