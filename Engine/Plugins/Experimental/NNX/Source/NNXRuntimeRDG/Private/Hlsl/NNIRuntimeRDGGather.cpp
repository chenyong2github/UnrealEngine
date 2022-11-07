// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGGather.h"
#include "NNXRuntimeHLSLHelper.h"
#include "NNEHlslShadersGatherCS.h"
#include "NNECoreAttributeMap.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorGather, TEXT("NNI.Operator.Hlsl.Gather"));

	/**
	 * Gather operator implementation
	 */
	template <typename DataElementType, typename IndicesElementType>
	class FGather : public NNX::FMLOperatorHlsl
	{
	public:

		FGather() {}
		virtual ~FGather() = default;

	private:

		int32 Axis = 0;
		NNX::FMLTensorDesc Data = {};
		NNX::FMLTensorDesc Indices = {};
		NNX::FMLTensorDesc Output = {};

	public:

		virtual bool Initialize(TArrayView<const NNX::FMLTensorDesc> InputTensors, TArrayView<const NNX::FMLTensorDesc> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			check(InputTensors.Num() == 2)
			check(OutputTensors.Num() == 1)
			check(Output.Shape.Num() <= FGatherConstants::MAX_NUM_DIMENSIONS)
			check(InputTensors[0].Shape.Num() > 0)
			check(InputTensors[1].Shape.Num() > 0)
			check(InputTensors[0].Shape.Num() + (InputTensors[1].Shape.Num() - 1) <= FGatherConstants::MAX_NUM_DIMENSIONS)

			Axis = Attributes.GetValueOrDefault(TEXT("axis"), Axis);
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
			using namespace UE::NNEHlslShaders::Internal;

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
		return new FGather<float, int32>();
	}

	bool RegisterGatherOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Gather"), CreateGatherOperator);
		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl