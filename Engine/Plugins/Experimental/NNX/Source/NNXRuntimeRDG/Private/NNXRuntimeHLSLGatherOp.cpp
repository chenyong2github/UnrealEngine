// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLGatherOp.h"
#include "NNXRuntimeHLSLHelper.h"
#include "NNXGatherCS.h"

namespace UE::NNI::RuntimeRDG::Hlsl::Private
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorGather, TEXT("NNI.Operator.Hlsl.Gather"));

	//template<typename DataElementType, typename IndicesElementType>
	using TGatherCS = typename UE::NNI::HlslShaders::Internal::TGatherCS;
	using FGatherConstants = UE::NNI::HlslShaders::Internal::FGatherConstants;

	template <typename DataElementType, typename IndicesElementType>
	class FOperatorGather : public NNX::FMLOperatorHlsl
	{
	public:
			
		virtual ~FOperatorGather() = default;

		FOperatorGather() {}

	private:

		int32 Axis = 0;
		NNX::FMLTensorDesc Data = {};
		NNX::FMLTensorDesc Indices = {};
		NNX::FMLTensorDesc Output = {};

	public:

		virtual bool Initialize(TArrayView<const NNX::FMLTensorDesc> InputTensors, TArrayView<const NNX::FMLTensorDesc> OutputTensors, const FMLAttributeMap& Attributes) override
		{
			check(InputTensors.Num() == 2)
			check(OutputTensors.Num() == 1)
			check(Output.Shape.Num() <= FGatherConstants::MAX_NUM_DIMENSIONS)
			check(InputTensors[0].Shape.Num() > 0)
			check(InputTensors[1].Shape.Num() > 0)
			check(InputTensors[0].Shape.Num() + (InputTensors[1].Shape.Num() - 1) <= FGatherConstants::MAX_NUM_DIMENSIONS)

			Axis = Attributes.GetOptionalFloat(TEXT("axis"), Axis);
			check(Axis < InputTensors[0].Shape.Num())
			check(Axis >= -InputTensors[0].Shape.Num())
			Axis = Axis >= 0 ? Axis : InputTensors[0].Shape.Num() + Axis;

			Data = InputTensors[0];
			Indices = InputTensors[1];
			Output = OutputTensors[0];

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
			// Set parameters
			TGatherCS::FParameters* Parameters = GraphBuilder.AllocParameters<TGatherCS::FParameters>();
			TGatherCS::FillInParameters(Axis, Data, Indices, *Parameters);
			Parameters->Data = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Parameters->Indices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			Parameters->Output = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			TGatherCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<TGatherCS::FGatherNumOutputDimensions>(Output.Shape.Num());
			TShaderMapRef<TGatherCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FIntVector ThreadGroupCount = TGatherCS::GetGroupCount(*Parameters);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.Gather");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorGather);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.Gather.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Parameters,
				ThreadGroupCount);
		}
	};

	NNX::FMLOperatorHlsl* CreateGatherOperator()
	{
		return new FOperatorGather<float, int32>();
	}

	bool RegisterGatherOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Gather"), CreateGatherOperator);
		return true;
	}
} // UE::NNI::RuntimeRDG::Hlsl::Private