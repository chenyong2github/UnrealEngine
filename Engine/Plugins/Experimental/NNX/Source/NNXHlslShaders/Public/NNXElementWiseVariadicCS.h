// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GlobalShader.h"
#include "NNXShaderParameters.h"

#include "NNXOperator.h"

class NNXHLSLSHADERS_API FMLElementWiseVariadicCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMLElementWiseVariadicCS);
	SHADER_USE_PARAMETER_STRUCT(FMLElementWiseVariadicCS, FGlobalShader)

public:

	NNXRT_ELEMENTWISEVARIADIC_PARAMETER_STRUCT()

	static const uint32 THREADGROUP_SIZE_X;

	class FOperatorType : SHADER_PERMUTATION_ENUM_CLASS("OP_TYPENAME", EMLElementWiseVariadicOperatorType);
	class FApplyScale : SHADER_PERMUTATION_BOOL("APPLYSCALE");
	class FNumInput : SHADER_PERMUTATION_RANGE_INT("NUMINPUT", 1, 4);
	using FPermutationDomain = TShaderPermutationDomain<FOperatorType, FApplyScale, FNumInput>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

private:

	static const FString GetOpFunc(EMLElementWiseVariadicOperatorType);
};

