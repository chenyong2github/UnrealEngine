// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGGather.h"
#include "NNERuntimeRDGHlslHelper.h"
#include "NNEHlslShadersGatherCS.h"
#include "NNECoreAttributeMap.h"
#include "NNECoreTypes.h"
#include "NNECoreTensor.h"

namespace UE::NNERuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNEOperatorGather, TEXT("NNE.Operator.Hlsl.Gather"));

	/**
	 * Gather operator implementation
	 */
	template <typename DataElementType, typename IndicesElementType>
	class FGather : public FOperatorHlsl
	{
	public:

		FGather() {}
		virtual ~FGather() = default;

	private:

		int32 Axis = 0;

	public:

		virtual int PrepareOutputs(TConstArrayView<NNECore::Internal::FTensorRef> InputTensors, TArrayView<NNECore::Internal::FTensorRef> OutputTensors) const override
		{
			check(InputTensors.Num() == 2)
			check(OutputTensors.Num() == 1)
			UE_LOG(LogNNE, Warning, TEXT("Gather shape inference is not implemented at the moment"));
			return -1;
		};
		
		virtual bool Initialize(TConstArrayView<NNECore::FTensorDesc> InputTensorDescs, TConstArrayView<NNECore::FTensorDesc> OutputTensorDescs, const NNECore::FAttributeMap& Attributes) override
		{
			const int32 MaxNumDimensions = NNEHlslShaders::Internal::FGatherConstants::MAX_NUM_DIMENSIONS;
			
			check(InputTensorDescs.Num() == 2)
			check(OutputTensorDescs.Num() == 1)

			const NNECore::FTensorDesc& Data = InputTensorDescs[0];
			const NNECore::FTensorDesc& Indices = InputTensorDescs[1];
			const NNECore::FTensorDesc& Output = OutputTensorDescs[0];

			if (Output.GetShape().Rank() <= MaxNumDimensions)
			{
				UE_LOG(LogNNE, Warning, TEXT("Gather first input should be of rank %d or less"), MaxNumDimensions);
				return false;
			}
			if (Data.GetShape().Rank() == 0)
			{
				UE_LOG(LogNNE, Warning, TEXT("Gather first input should be at least of rank 1"));
				return false;
			}
			if (Indices.GetShape().Rank() == 0)
			{
				UE_LOG(LogNNE, Warning, TEXT("Gather second input should be at least of rank 1"));
				return false;
			}
			if ((Data.GetShape().Rank() + Indices.GetShape().Rank() - 1) > MaxNumDimensions)
			{
				UE_LOG(LogNNE, Warning, TEXT("Gather sum of input 0 and 1 ranks -1 should be less than %d"), MaxNumDimensions);
				return false;
			}

			Axis = Attributes.GetValueOrDefault(TEXT("axis"), Axis);
			if (Axis >= Data.GetShape().Rank())
			{
				UE_LOG(LogNNE, Warning, TEXT("Gather Axis attribute should be inferior to first input rank"));
				return false;
			}
			if (Axis < -Data.GetShape().Rank())
{
				UE_LOG(LogNNE, Warning, TEXT("Gather Axis attribute should be superior or equal to minus the first input rank"));
				return false;
			}
			Axis = Axis >= 0 ? Axis : Data.GetShape().Rank() + Axis;

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDGRef> InputTensors, TConstArrayView<FTensorRDGRef> OutputTensors) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			check(InputTensors.Num() == 2)
			check(OutputTensors.Num() == 1)
			check(InputTensors[0] != nullptr);
			check(InputTensors[1] != nullptr);
			check(OutputTensors[0] != nullptr);
			check(OutputTensors[0]->GetShape().Rank() <= FGatherConstants::MAX_NUM_DIMENSIONS)
			check(InputTensors[0]->GetShape().Rank() > 0)
			check(InputTensors[1]->GetShape().Rank() > 0)
			check(InputTensors[0]->GetShape().Rank() + (InputTensors[1]->GetShape().Rank() - 1) <= FGatherConstants::MAX_NUM_DIMENSIONS)

			const FTensorRDG& Data = *InputTensors[0];
			const FTensorRDG& Indices = *InputTensors[1];
			const FTensorRDG& Output = *OutputTensors[0];

			// Set parameters
			TGatherCS::FParameters* Parameters = GraphBuilder.AllocParameters<TGatherCS::FParameters>();
			TGatherCS::FillInParameters(Axis, Data, Indices, *Parameters);
			Parameters->Data = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Data.GetBuffer(), PF_R32_FLOAT));
			Parameters->Indices = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Indices.GetBuffer(), PF_R32_FLOAT));
			Parameters->Output = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.GetBuffer(), PF_R32_FLOAT));

			TGatherCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<TGatherCS::FGatherNumOutputDimensions>(Output.GetShape().Rank());
			TShaderMapRef<TGatherCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FIntVector ThreadGroupCount = TGatherCS::GetGroupCount(*Parameters);

			RDG_EVENT_SCOPE(GraphBuilder, "NNE.Operator.Hlsl.Gather");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNEOperatorGather);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNE.Operator.Hlsl.Gather.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Parameters,
				ThreadGroupCount);
		}
	};

	bool ValidateGatherOperator(const NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNECore::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("axis"), ENNEAttributeDataType::Int32);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		FInputValidator InputValidator;
		InputValidator.AddSupportedType(ENNETensorDataType::Float);
		InputValidator.AddRequired();
		InputValidator.AddRequired();//Indices should be int32 or int64
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	FOperatorHlsl* CreateGatherOperator()
	{
		return new FGather<float, int32>();
	}

	bool RegisterGatherOperator(FOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Gather"), CreateGatherOperator, ValidateGatherOperator);
		return true;
	}
} // UE::NNERuntimeRDG::Private::Hlsl