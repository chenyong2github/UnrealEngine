// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "NNXShaderParameters.h"

#include "NNXOperator.h"

//
//
//
class NNXHLSLSHADERS_API FMLElementWiseCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLElementWiseCS);
	SHADER_USE_PARAMETER_STRUCT(FMLElementWiseCS, FGlobalShader)

public:

	NNXRT_ELEMENTWISEUNARY_PARAMETER_STRUCT()

	static const uint32 THREADGROUP_SIZE_X;

	class FOperatorType : SHADER_PERMUTATION_ENUM_CLASS("OP_TYPENAME", EMLElementWiseUnaryOperatorType);
	using FPermutationDomain = TShaderPermutationDomain<FOperatorType>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

	

private:

	static const FString GetOpFunc(EMLElementWiseUnaryOperatorType OpType);
};

