// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXElementWiseVariadicCS.h"
#include "NNXCore.h"

const uint32 FMLElementWiseVariadicCS::THREADGROUP_SIZE_X(128);

void FMLElementWiseVariadicCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);

    OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), THREADGROUP_SIZE_X);

    FPermutationDomain PermutationVector(InParameters.PermutationId);

	const FString OpFunc = GetOpFunc(PermutationVector.Get<FOperatorType>());

	OutEnvironment.SetDefine(TEXT("ELEMENTWISE_OP(X,Y)"), *OpFunc);
}

#define OP(OpName, OpFunc) OpTable[(int32) EMLElementWiseVariadicOperatorType::OpName] = OpFunc

const FString FMLElementWiseVariadicCS::GetOpFunc(EMLElementWiseVariadicOperatorType OpType)
{
	FString OpTable[(int32)EMLElementWiseVariadicOperatorType::MAX];

	for (int32 Idx = 0; Idx < (int32)EMLElementWiseVariadicOperatorType::MAX; ++Idx)
	{
		OpTable[Idx] = FString("");
	}

	OP(Max, TEXT("max(X,Y)"));
	OP(Min, TEXT("min(X,Y)"));
	OP(Mean, TEXT("((X)+(Y))"));
	OP(Sum, TEXT("((X)+(Y))"));

	FString OpFunc = OpTable[(int32)OpType];

	if (OpFunc == "")
	{
		UE_LOG(LogNNX, Warning, TEXT("Undefined ElementWise Variadic operator name for operator:%d"), OpType);
	}

	return OpFunc;
}

#undef OP

IMPLEMENT_GLOBAL_SHADER(FMLElementWiseVariadicCS, "/NNX/ElementWiseVariadicOp.usf", "ElementWiseVariadicOp", SF_Compute);
