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

	static const uint32 THREADGROUP_SIZE_X;
	static const uint32 MAX_NUM_INPUT = 4;

	class FOperatorType : SHADER_PERMUTATION_ENUM_CLASS("OP_TYPENAME", EMLElementWiseVariadicOperatorType);
	class FApplyScale : SHADER_PERMUTATION_BOOL("APPLYSCALE");
	class FOutputAsInput : SHADER_PERMUTATION_BOOL("OUTPUTASINPUT");
	class FNumInput : SHADER_PERMUTATION_RANGE_INT("NUMINPUT", 1, MAX_NUM_INPUT);
	using FPermutationDomain = TShaderPermutationDomain<FOperatorType, FApplyScale, FOutputAsInput, FNumInput>;

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment);

	NNXRT_ELEMENTWISEVARIADIC_PARAMETER_STRUCT()

private:

	static const FString GetOpFunc(EMLElementWiseVariadicOperatorType);
};

