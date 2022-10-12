// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLElementWiseUnaryOps.h"
#include "NNXElementWiseCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace NNX
{
DECLARE_GPU_STAT_NAMED(FMLHLSLOperatorElementWiseUnary, TEXT("FML.HLSL.Operator.ElementWise.Unary"));

/**
 * Unary Element-wise ML operator implementation
 */
template<EMLElementWiseUnaryOperatorType OpType>
class FMLOperatorHlslElementWiseUnary : public FMLOperatorHlsl
{
public:

	static FMLOperatorHlsl* Create()
	{
		return new FMLOperatorHlslElementWiseUnary();
	}

	virtual ~FMLOperatorHlslElementWiseUnary() = default;

private:

	FMLOperatorHlslElementWiseUnary() : Alpha(0.0f), Beta(0.0f), Gamma(0.0f) {}
	float Alpha;
	float Beta;
	float Gamma;
	FMLTensorDesc Input;
	FMLTensorDesc Output;

public:

	virtual bool Initialize(TArrayView<const FMLTensorDesc> InputTensors, TArrayView<const FMLTensorDesc> OutputTensors, const FMLAttributeMap& Attributes) override
	{
		check(InputTensors.Num() == 1);
		check(OutputTensors.Num() == 1);

		Input = InputTensors[0];
		Output = OutputTensors[0];

		Alpha = Attributes.GetOptionalFloat(TEXT("alpha"), Alpha);
		Beta = Attributes.GetOptionalFloat(TEXT("beta"), Beta);
		Gamma = Attributes.GetOptionalFloat(TEXT("gamma"), Gamma);

		return true;
	}

	virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override
	{
		// HACK: This only works for single layer networks
		FRDGBufferSRVRef InputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
		FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));
		
		FIntVector ThreadGroupCount = ComputeElementWiseThreadGroups(Output.Num(), FMLElementWiseCS::THREADGROUP_SIZE_X);

		// Set parameters
		FMLElementWiseCS::FParameters* Params = GraphBuilder.AllocParameters<FMLElementWiseCS::FParameters>();
		Params->Input = InputSRV;
		Params->Output = OutputUAV;
		Params->Alpha = Alpha;
		Params->Beta = Beta;
		Params->Gamma = Gamma;
		Params->Num = Output.Num();
		Params->ThreadCountX = ThreadGroupCount.X * FMLElementWiseCS::THREADGROUP_SIZE_X;

		FMLElementWiseCS::FPermutationDomain PermutationVector;

		PermutationVector.Set<FMLElementWiseCS::FOperatorType>(OpType);

		TShaderMapRef<FMLElementWiseCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

		RDG_EVENT_SCOPE(GraphBuilder, "FML.HLSL.Operator.ElementWise.Unary");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FMLHLSLOperatorElementWiseUnary);
		
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FML.HLSL.Operator.ElementWise.Unary.Dispatch"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			ComputeShader,
			Params,
			ThreadGroupCount);
	}
};

template<> FMLOperatorHlslElementWiseUnary<EMLElementWiseUnaryOperatorType::Selu>::FMLOperatorHlslElementWiseUnary()
	: Alpha(1.67326319217681884765625f), Beta(0.0f), Gamma(1.05070102214813232421875f)
{
}

template<> FMLOperatorHlslElementWiseUnary<EMLElementWiseUnaryOperatorType::Elu>::FMLOperatorHlslElementWiseUnary()
	: Alpha(1.0f), Beta(0.0f), Gamma(0.0f) 
{
}

template<> FMLOperatorHlslElementWiseUnary<EMLElementWiseUnaryOperatorType::HardSigmoid>::FMLOperatorHlslElementWiseUnary()
	: Alpha(0.2f), Beta(0.5f), Gamma(0.0f)
{
}

template<> FMLOperatorHlslElementWiseUnary<EMLElementWiseUnaryOperatorType::LeakyRelu>::FMLOperatorHlslElementWiseUnary()
	: Alpha(0.01f), Beta(0.0f), Gamma(0.0f)
{
}

bool RegisterElementWiseUnaryOperators(FMLOperatorRegistryHlsl& Registry)
{
	#define OP(Name) Registry.OpAdd(TEXT(#Name), FMLOperatorHlslElementWiseUnary<EMLElementWiseUnaryOperatorType::Name>::Create)

	OP(Abs);
	OP(Acos);
	OP(Acosh);
	OP(Asin);
	OP(Asinh);
	OP(Atan);
	OP(Atanh);
	//OP(BitShift);
	//OP(Cast);
	OP(Ceil);
	//OP(Clip);
	OP(Cos);
	OP(Cosh);
	OP(Elu);
	OP(Erf);
	OP(Exp);
	OP(Floor);
	OP(IsInf);
	OP(IsNan);
	OP(HardSigmoid);
	OP(HardSwish);
	OP(LeakyRelu);
	OP(Log);
	OP(Neg);
	//OP(Not);
	OP(Reciprocal);
	OP(Relu);
	OP(Round);
	OP(Selu);
	OP(Sigmoid);
	OP(Sign);
	OP(Sin);
	OP(Sinh);
	OP(Softplus);
	OP(Softsign);
	OP(Sqrt);
	OP(Tan);
	OP(Tanh);

	#undef OP

	return true;
}

} // NNX
