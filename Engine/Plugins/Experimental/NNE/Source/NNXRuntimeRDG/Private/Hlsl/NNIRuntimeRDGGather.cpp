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

	public:

		virtual int ComputeOutputShape(TConstArrayView<NNX::FTensorShape> InputShapes, TArray<NNX::FTensorShape>& OutputShapes) const override
		{
			OutputShapes.Empty();
			UE_LOG(LogNNX, Warning, TEXT("Gather does not support variable input shapes at the moment"));
			return -1;
		};
		
		virtual bool Initialize(TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) override
		{
			const int32 MaxNumDimensions = UE::NNEHlslShaders::Internal::FGatherConstants::MAX_NUM_DIMENSIONS;
			
			check(InputTensorDescs.Num() == 2)
			check(OutputTensorDescs.Num() == 1)

			const NNX::FTensorDesc& Data = InputTensorDescs[0];
			const NNX::FTensorDesc& Indices = InputTensorDescs[1];
			const NNX::FTensorDesc& Output = OutputTensorDescs[0];

			if (Output.GetShape().Rank() <= MaxNumDimensions)
			{
				UE_LOG(LogNNX, Warning, TEXT("Gather first input should be of rank %d or less"), MaxNumDimensions);
				return false;
			}
			if (Data.GetShape().Rank() == 0)
			{
				UE_LOG(LogNNX, Warning, TEXT("Gather first input should be at least of rank 1"));
				return false;
			}
			if (Indices.GetShape().Rank() == 0)
			{
				UE_LOG(LogNNX, Warning, TEXT("Gather second input should be at least of rank 1"));
				return false;
			}
			if ((Data.GetShape().Rank() + Indices.GetShape().Rank() - 1) > MaxNumDimensions)
			{
				UE_LOG(LogNNX, Warning, TEXT("Gather sum of input 0 and 1 ranks -1 should be less than %d"), MaxNumDimensions);
				return false;
			}

			Axis = Attributes.GetValueOrDefault(TEXT("axis"), Axis);
			if (Axis >= Data.GetShape().Rank())
			{
				UE_LOG(LogNNX, Warning, TEXT("Gather Axis attribute should be inferior to first input rank"));
				return false;
			}
			if (Axis < -Data.GetShape().Rank())
{
				UE_LOG(LogNNX, Warning, TEXT("Gather Axis attribute should be superior or equal to minus the first input rank"));
				return false;
			}
			Axis = Axis >= 0 ? Axis : Data.GetShape().Rank() + Axis;

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FTensorRDG> InputTensors, TConstArrayView<NNX::FTensorRDG> OutputTensors) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			check(InputTensors.Num() == 2)
			check(OutputTensors.Num() == 1)
			check(OutputTensors[0].GetShape().Rank() <= FGatherConstants::MAX_NUM_DIMENSIONS)
			check(InputTensors[0].GetShape().Rank() > 0)
			check(InputTensors[1].GetShape().Rank() > 0)
			check(InputTensors[0].GetShape().Rank() + (InputTensors[1].GetShape().Rank() - 1) <= FGatherConstants::MAX_NUM_DIMENSIONS)

			const NNX::FTensorRDG& Data = InputTensors[0];
			const NNX::FTensorRDG& Indices = InputTensors[1];
			const NNX::FTensorRDG& Output = OutputTensors[0];

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

	bool ValidateGatherOperator(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<NNX::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("axis"), ENNEAttributeDataType::Int32);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(EMLTensorDataType::Float);
		InputValidator.AddRequired();
		InputValidator.AddRequired();//Indices should be int32 or int64
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	NNX::FMLOperatorHlsl* CreateGatherOperator()
	{
		return new FGather<float, int32>();
	}

	bool RegisterGatherOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Gather"), CreateGatherOperator, ValidateGatherOperator);
		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl