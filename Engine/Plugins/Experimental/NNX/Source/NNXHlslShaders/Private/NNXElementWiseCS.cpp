// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXElementWiseCS.h"
#include "NNXCore.h"

//
//
//
const uint32 FMLElementWiseCS::THREADGROUP_SIZE_X(128);

//
//
//
void FMLElementWiseCS::ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& InParameters, FShaderCompilerEnvironment& OutEnvironment)
{
    FGlobalShader::ModifyCompilationEnvironment(InParameters, OutEnvironment);

    OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), THREADGROUP_SIZE_X);

    FPermutationDomain PermutationVector(InParameters.PermutationId);

    const FString OpFunc = GetOpFunc(PermutationVector.Get<FOperatorType>());

    OutEnvironment.SetDefine(TEXT("ELEMENTWISE_OP(X)"), *OpFunc);
}

#define OP(OpName, OpFunc) OpTable[(int32) EMLElementWiseUnaryOperatorType::OpName] = OpFunc

//
//
//
const FString FMLElementWiseCS::GetOpFunc(EMLElementWiseUnaryOperatorType OpType)
{
    FString OpTable[(int32) EMLElementWiseUnaryOperatorType::MAX];

    for (int32 Idx = 0; Idx < (int32) EMLElementWiseUnaryOperatorType::MAX; ++Idx)
    {
        OpTable[Idx] = FString("");
    }

    OP(Abs,         TEXT("abs(X)"));
    OP(Acos,        TEXT("acos(X)"));
    OP(Acosh,       TEXT("acosh(X)"));
    OP(Asin,        TEXT("asin(X)"));
    OP(Asinh,       TEXT("asinh(X)"));
    OP(Atan,        TEXT("atan(X)"));
    OP(Atanh,       TEXT("atanh(X)"));
    //OP(BitShift,  TEXT("bitshift(X)"));
    //OP(Cast,      TEXT("cast(X)"));
    OP(Ceil,        TEXT("ceil(X)"));
    //OP(Clip,      TEXT("clip(X)"));
    OP(Cos,         TEXT("cos(X)"));
    OP(Cosh,        TEXT("cosh(X)"));
    OP(Elu,         TEXT("elu(X)"));
    OP(Erf,         TEXT("erf(X)"));
    OP(Exp,         TEXT("exp(X)"));
    OP(Floor,       TEXT("floor(X)"));
    OP(IsInf,       TEXT("isinf(X)"));
    OP(IsNan,       TEXT("isnan(X)"));//TODO check shader there is a warning saying input can neither be Nan on PC FXC
    OP(HardSigmoid, TEXT("hardSigmoid(X)"));
    OP(HardSwish,   TEXT("hardSwish(X)"));
    OP(LeakyRelu,   TEXT("leakyRelu(X)"));
    OP(Log,         TEXT("log(X)"));
    OP(Neg,         TEXT("-(X)"));
    //OP(Not,       TEXT("not(X)"));
    OP(Reciprocal,  TEXT("1 / (X)"));
    OP(Relu,        TEXT("relu(X)"));
    OP(Round,       TEXT("round(X)"));
    OP(Selu,        TEXT("selu(X)"));
    OP(Sigmoid,     TEXT("sigmoid(X)"));
    OP(Sign,        TEXT("sign(X)"));
    OP(Sin,         TEXT("sin(X)"));
    OP(Sinh,        TEXT("sinh(X)"));
    OP(Softplus,    TEXT("softplus(X)"));
    OP(Softsign,    TEXT("softsign(X)"));
    OP(Sqrt,        TEXT("sqrt(X)"));
    OP(Tan,         TEXT("tan(X)"));
    OP(Tanh,        TEXT("tanh(X)"));

    FString OpFunc = OpTable[(int32) OpType];

    if (OpFunc == "")
    {
        UE_LOG(LogNNX, Warning, TEXT("Undefined ElementWise operator name for operator:%d"), OpType);
    }

    return OpFunc;
}

#undef OP

//
//
//
IMPLEMENT_GLOBAL_SHADER(FMLElementWiseCS, "/NNX/ElementWiseOp.usf", "ElementWiseOp", SF_Compute);

////
////
////
//namespace NNX
//{
//
//NNXHLSLSHADERS_API void AddHlslIdentityOpPass(FRDGBuilder& GraphBuilder, const IdentityOpArgs& Args)
//{
//	// Set parameters
//	FIdentityOpCS::FParameters* Parameters = GraphBuilder.AllocParameters<FIdentityOpCS::FParameters>();
//
//	Parameters->Input = Args.Input;
//	Parameters->Output = Args.Output;
//	Parameters->Num = Args.Num;
//
//	TShaderMapRef<FIdentityOpCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
//
//	const uint32 ThreadGroupCountValueX = FMath::DivideAndRoundUp(Args.Num, FIdentityOpCS::THREADGROUP_SIZE_X);
//	
//	FComputeShaderUtils::AddPass(
//		GraphBuilder,
//		RDG_EVENT_NAME("FIdentityCS()"),
//		ComputeShader,
//		Parameters,
//		FIntVector(ThreadGroupCountValueX, 1, 1));
//}
//
//} // NNX namespace
