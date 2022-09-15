// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "NNXShaderParameters.h"

#include "NNXOperator.h"

//
//
//
class NNXHLSLSHADERS_API FMLElementWiseBinaryCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLElementWiseBinaryCS);
	SHADER_USE_PARAMETER_STRUCT(FMLElementWiseBinaryCS, FGlobalShader)

public:

	NNXRT_ELEMENTWISEBINARY_PARAMETER_STRUCT()
	
	static const uint32 THREADGROUP_SIZE_X;

	class FOperatorType : SHADER_PERMUTATION_ENUM_CLASS("OP_TYPENAME", EMLElementWiseBinaryOperatorType);
	using FPermutationDomain = TShaderPermutationDomain<FOperatorType>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

private:

	static const FString GetOpFunc(EMLElementWiseBinaryOperatorType);
};

