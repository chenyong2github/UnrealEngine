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
		NNX::FTensor Data = {};
		NNX::FTensor Indices = {};
		NNX::FTensor Output = {};

	public:

		virtual bool Initialize(TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) override
		{
			TArray<NNX::FTensor> InputTensors;
			TArray<NNX::FTensor> OutputTensors;
			if (!NNX::ConvertConcreteTensorDescsToTensors(InputTensorDescs, InputTensors) ||
				!NNX::ConvertConcreteTensorDescsToTensors(OutputTensorDescs, OutputTensors))
			{
				UE_LOG(LogNNX, Warning, TEXT("Variable input shapes are not supported by this operator"));
				return false;
			}
			
			using namespace UE::NNEHlslShaders::Internal;

			check(InputTensors.Num() == 2)
			check(OutputTensors.Num() == 1)
			check(Output.GetShape().Rank() <= FGatherConstants::MAX_NUM_DIMENSIONS)
			check(InputTensors[0].GetShape().Rank() > 0)
			check(InputTensors[1].GetShape().Rank() > 0)
			check(InputTensors[0].GetShape().Rank() + (InputTensors[1].GetShape().Rank() - 1) <= FGatherConstants::MAX_NUM_DIMENSIONS)

			Axis = Attributes.GetValueOrDefault(TEXT("axis"), Axis);
			check(Axis < InputTensors[0].GetShape().Rank())
			check(Axis >= -InputTensors[0].GetShape().Rank())
			Axis = Axis >= 0 ? Axis : InputTensors[0].GetShape().Rank() + Axis;

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
			PermutationVector.Set<TGatherCS::FGatherNumOutputDimensions>(Output.GetShape().Rank());
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