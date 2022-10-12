// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLElementWiseBinaryOps.h"
#include "NNXElementWiseBinaryCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace NNX
{
DECLARE_GPU_STAT_NAMED(FMLHLSLOperatorElementWiseBinary, TEXT("FML.HLSL.Operator.ElementWise.Binary"));

/**
 * Binary Element-wise ML operator implementation
 */
template<EMLElementWiseBinaryOperatorType OpType>
class FMLOperatorHlslElementWiseBinary : public FMLOperatorHlsl
{
public:

	static FMLOperatorHlsl* Create()
	{
		return new FMLOperatorHlslElementWiseBinary();
	}

	virtual ~FMLOperatorHlslElementWiseBinary() = default;

private:

	FMLOperatorHlslElementWiseBinary() {}

	FMLTensorDesc LHSInput;
	FMLTensorDesc RHSInput;
	FMLTensorDesc Output;

public:

	virtual bool Initialize(TArrayView<const FMLTensorDesc> InputTensors, TArrayView<const FMLTensorDesc> OutputTensors, const FMLAttributeMap& Attributes) override
	{
		check(InputTensors.Num() == 2);
		check(OutputTensors.Num() == 1);
		
		LHSInput = InputTensors[0];
		RHSInput = InputTensors[1];
		Output = OutputTensors[0];

		return true;
	}

	virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override
	{
		// HACK: This only works for single layer networks
		FRDGBufferSRVRef LHSInputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
		FRDGBufferSRVRef RHSInputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
		FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

		FIntVector ThreadGroupCount = ComputeElementWiseThreadGroups(Output.Num(), FMLElementWiseBinaryCS::THREADGROUP_SIZE_X);

		// Set parameters
		FMLElementWiseBinaryCS::FParameters* Params = GraphBuilder.AllocParameters<FMLElementWiseBinaryCS::FParameters>();
		Params->LHSInput = LHSInputSRV;
		Params->RHSInput = RHSInputSRV;
		Params->Output = OutputUAV;
		FillTensorStrideForBroadcastShaderParameters(LHSInput, Output.Dimension, Params->TensorInfo, 0);
		FillTensorStrideForBroadcastShaderParameters(RHSInput, Output.Dimension, Params->TensorInfo, 1);
		FillTensorStrideShaderParameters(Output, Params->TensorInfo, 2);
		Params->Num = Output.Num();
		Params->ThreadCountX = ThreadGroupCount.X * FMLElementWiseBinaryCS::THREADGROUP_SIZE_X;

		FMLElementWiseBinaryCS::FPermutationDomain PermutationVector;

		PermutationVector.Set<FMLElementWiseBinaryCS::FOperatorType>(OpType);
		PermutationVector.Set<FMLElementWiseBinaryCS::FBinaryNumDimensions>(Output.Dimension);

		TShaderMapRef<FMLElementWiseBinaryCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		
		RDG_EVENT_SCOPE(GraphBuilder, "FML.HLSL.Operator.ElementWise.Binary");
		RDG_GPU_STAT_SCOPE(GraphBuilder, FMLHLSLOperatorElementWiseBinary);

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("FML.HLSL.Operator.ElementWise.Binary.Dispatch"),
			ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
			ComputeShader,
			Params,
			ThreadGroupCount);
	}
};

bool RegisterElementWiseBinaryOperators(FMLOperatorRegistryHlsl& Registry)
{
	#define OP(Name) Registry.OpAdd(TEXT(#Name), FMLOperatorHlslElementWiseBinary<EMLElementWiseBinaryOperatorType::Name>::Create)

	OP(Add);
	//OP(And);
	OP(Div);
	//OP(Equal);
	//OP(Greater);
	//OP(GreaterOrEqual);
	//OP(Less);
	//OP(LessOrEqual);
	OP(Mod);
	OP(Mul);
	//OP(Or);
	OP(Prelu);
	OP(Pow);
	OP(Sub);
	//OP(Or);

	#undef OP

	return true;
}

} // NNX
