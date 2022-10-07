// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXElementWiseBinaryCS.h"
#include "NNXCore.h"

//
//
//
const uint32 FMLElementWiseBinaryCS::THREADGROUP_SIZE_X(128);

//
//
//
void FMLElementWiseBinaryCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);

    OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), THREADGROUP_SIZE_X);

    FPermutationDomain PermutationVector(InParameters.PermutationId);

    const FString OpFunc = GetOpFunc(PermutationVector.Get<FOperatorType>());

    OutEnvironment.SetDefine(TEXT("ELEMENTWISE_OP(X,Y)"), *OpFunc);
}

#define OP(OpName, OpFunc) OpTable[(int32) EMLElementWiseBinaryOperatorType::OpName] = OpFunc

//
//
//
const FString FMLElementWiseBinaryCS::GetOpFunc(EMLElementWiseBinaryOperatorType OpType)
{
    FString OpTable[(int32) EMLElementWiseBinaryOperatorType::MAX];

    for (int32 Idx = 0; Idx < (int32)EMLElementWiseBinaryOperatorType::MAX; ++Idx)
    {
        OpTable[Idx] = FString("");
    }

    OP(Add,              TEXT("((X)+(Y))"));
	//OP(And,            TEXT("((X)&&(Y))"));
	OP(Div,              TEXT("((X)/(Y))"));
	//OP(Equal,          TEXT("((X)==(Y))"));
	//OP(Greater,        TEXT("((X)>(Y))"));
	//OP(GreaterOrEqual, TEXT("((X)>=(Y))"));
	//OP(Less,           TEXT("((X)<(Y))"));
	//OP(LessOrEqual,    TEXT("((X)<(Y))"));
	OP(Mod,              TEXT("((X)%(Y))"));
	OP(Mul,              TEXT("((X)*(Y))"));
	//OP(Or,             TEXT("((X)||(Y))"));
	OP(Prelu,            TEXT("prelu(X,Y)"));
	OP(Pow,              TEXT("safe_pow(X,Y)"));
	OP(Sub,              TEXT("((X)-(Y))"));
	//OP(Or,             TEXT("((X)^=(Y))"));

    FString OpFunc = OpTable[(int32) OpType];

    if (OpFunc == "")
    {
        UE_LOG(LogNNX, Warning, TEXT("Undefined ElementWise Binary operator name for operator:%d"), OpType);
    }

    return OpFunc;
}

#undef OP

//
//
//
IMPLEMENT_GLOBAL_SHADER(FMLElementWiseBinaryCS, "/NNX/ElementWiseBinaryOp.usf", "ElementWiseBinaryOp", SF_Compute);
