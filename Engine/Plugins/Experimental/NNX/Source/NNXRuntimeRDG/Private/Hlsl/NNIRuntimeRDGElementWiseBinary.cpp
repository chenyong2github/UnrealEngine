// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGElementWiseBinary.h"
#include "NNEHlslShadersElementWiseBinaryCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorElementWiseBinary, TEXT("NNI.Operator.Hlsl.ElementWise.Binary"));

	using TElementWiseBinaryCS = typename UE::NNEHlslShaders::Internal::TElementWiseBinaryCS;
	using FElementWiseBinaryConstants = UE::NNEHlslShaders::Internal::FElementWiseBinaryConstants;

	/**
	 * Binary element-wise operator implementation
	 */
	template<EMLElementWiseBinaryOperatorType OpType>
	class TElementWiseBinary : public NNX::FMLOperatorHlsl
	{
	public:

		TElementWiseBinary() {}
		virtual ~TElementWiseBinary() = default;

	private:

		NNX::FMLTensorDesc LHSInput = {};
		NNX::FMLTensorDesc RHSInput = {};
		NNX::FMLTensorDesc Output = {};

	public:

		virtual bool Initialize(TArrayView<const NNX::FMLTensorDesc> InputTensors, TArrayView<const NNX::FMLTensorDesc> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensors.Num() == 2);
			check(OutputTensors.Num() == 1);
		
			LHSInput = InputTensors[0];
			RHSInput = InputTensors[1];
			Output = OutputTensors[0];

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
			// HACK: This only works for single layer networks
			FRDGBufferSRVRef LHSInputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			FRDGBufferSRVRef RHSInputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			FIntVector ThreadGroupCount = NNX::ComputeElementWiseThreadGroups(Output.Volume, FElementWiseBinaryConstants::NUM_GROUP_THREADS);

			// Set parameters
			TElementWiseBinaryCS::FParameters* Params = GraphBuilder.AllocParameters<TElementWiseBinaryCS::FParameters>();
			Params->LHSInput = LHSInputSRV;
			Params->RHSInput = RHSInputSRV;
			Params->Output = OutputUAV;
			FillTensorStrideForBroadcastShaderParameters(LHSInput, Output.Shape.Num(), Params->TensorInfo, 0);
			FillTensorStrideForBroadcastShaderParameters(RHSInput, Output.Shape.Num(), Params->TensorInfo, 1);
			FillTensorStrideShaderParameters(Output, Params->TensorInfo, 2);
			Params->Num = Output.Volume;
			Params->ThreadCountX = ThreadGroupCount.X * FElementWiseBinaryConstants::NUM_GROUP_THREADS;

			TElementWiseBinaryCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<TElementWiseBinaryCS::FOperatorType>(OpType);
			PermutationVector.Set<TElementWiseBinaryCS::FBinaryNumDimensions>(Output.Shape.Num());

			TShaderMapRef<TElementWiseBinaryCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		
			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.ElementWise.Binary");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorElementWiseBinary);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.ElementWise.Binary.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				ThreadGroupCount);
		}
	};

	template<EMLElementWiseBinaryOperatorType OpType>
	NNX::FMLOperatorHlsl* CreateElementWiseBinaryOperator()
	{
		return new TElementWiseBinary<OpType>();
	}

	bool RegisterElementWiseBinaryOperators(NNX::FMLOperatorRegistryHlsl& Registry)
	{
#define OP(Name) Registry.OpAdd(TEXT(#Name), CreateElementWiseBinaryOperator<EMLElementWiseBinaryOperatorType::Name>)
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
} // UE::NNIRuntimeRDG::Private::Hlsl
