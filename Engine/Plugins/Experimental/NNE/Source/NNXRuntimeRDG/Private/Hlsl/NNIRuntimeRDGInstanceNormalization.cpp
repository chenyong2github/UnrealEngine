// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGInstanceNormalization.h"
#include "NNEHlslShadersInstanceNormalizationCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace
{

int ValidateInput(const NNX::FTensorShape& Input, const NNX::FTensorShape& Scale, const NNX::FTensorShape& Bias)
{
	if (Input.Rank() < 3)
	{
		UE_LOG(LogNNX, Warning, TEXT("InstanceNormalization input data should be at least of rank 4: %d"), Input.Rank());
		return -1;
	}

	if (Scale.Rank() != 1)
	{
		UE_LOG(LogNNX, Warning, TEXT("InstanceNormalization input scale should be of rank 1: %d"), Scale.Rank());
		return -1;
	}
	if (Scale.Data[0] != Input.Data[1])
	{
		UE_LOG(LogNNX, Warning, TEXT("InstanceNormalization input scale size should be equal to channel count: %d vs %d"), Scale.Data[0], Input.Data[1]);
		return -1;
	}

	if (Bias.Data[0] != Input.Data[1])
	{
		UE_LOG(LogNNX, Warning, TEXT("InstanceNormalization intput B size should be equal to channel count: : %d vs %d"), Bias.Data[0], Input.Data[1]);
		return -1;
	}
	if (Bias.Rank() != 1)
	{
		UE_LOG(LogNNX, Warning, TEXT("InstanceNormalization input B should be of rank 1: %d"), Bias.Rank());
		return -1;
	}

	return 0;
}

}

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorInstanceNormalization, TEXT("NNI.Operator.Hlsl.InstanceNormalization"));

	using EInstanceNormalizationAlgorithm = UE::NNEHlslShaders::Internal::EInstanceNormalizationAlgorithm;

	/**
	 * InstanceNormalization operator implementation
	 */
	class TInstanceNormalization : public NNX::FMLOperatorHlsl
	{
	public:

		TInstanceNormalization() {}
		virtual ~TInstanceNormalization() = default;

	private:

		float Epsilon = 1e-5;

		EInstanceNormalizationAlgorithm Algorithm = EInstanceNormalizationAlgorithm::MAX;

	public:

		virtual int PrepareOutputs(TConstArrayView<NNX::FTensorRef> InputTensors, TArrayView<NNX::FTensorRef> OutputTensors) const override
		{
			check(InputTensors.Num() == 3);
			check(OutputTensors.Num() == 1);

			if (const int Res = ValidateInput(InputTensors[0]->GetShape(), InputTensors[1]->GetShape(), InputTensors[2]->GetShape()); Res < 0)
			{
				return Res;
			}

			OutputTensors[0]->SetShape(InputTensors[0]->GetShape());

			return 0;
		};

		virtual bool Initialize(TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			check(InputTensorDescs.Num() == 3);
			check(OutputTensorDescs.Num() == 1);

			Epsilon = Attributes.GetValue<float>(TEXT("epsilon"));

			// For testing only
			TInstanceNormalizationCS::LexFromString(Algorithm, *Attributes.GetValueOrDefault<FString>(TEXT("__UE__algorithm"), "MAX"));

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FTensorRDGRef> InputTensors, TConstArrayView<NNX::FTensorRDGRef> OutputTensors) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			check(InputTensors.Num() == 3);
			check(OutputTensors.Num() == 1);
			check(InputTensors[0] != nullptr);
			check(InputTensors[1] != nullptr);
			check(InputTensors[2] != nullptr);
			check(OutputTensors[0] != nullptr);

			const NNX::FTensorRDG& Input = *InputTensors[0];
			const NNX::FTensorRDG& Scale = *InputTensors[1];
			const NNX::FTensorRDG& Bias = *InputTensors[2];
			const NNX::FTensorRDG& Output = *OutputTensors[0];

			// Set parameters
			TInstanceNormalizationCS::FParameters* Parameters = GraphBuilder.AllocParameters<TInstanceNormalizationCS::FParameters>();
			TInstanceNormalizationCS::FillInParameters(Epsilon, Input, *Parameters);
			Parameters->Input = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Input.GetBuffer(), PF_R32_FLOAT));
			Parameters->Scale = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Scale.GetBuffer(), PF_R32_FLOAT));
			Parameters->Bias = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Bias.GetBuffer(), PF_R32_FLOAT));
			Parameters->Output = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.GetBuffer(), PF_R32_FLOAT));

			EInstanceNormalizationAlgorithm DispatchAlgorithm = Algorithm;
			if (DispatchAlgorithm == EInstanceNormalizationAlgorithm::MAX)
			{
				DispatchAlgorithm = TInstanceNormalizationCS::GetAlgorithm(*Parameters);
			}
			
			TInstanceNormalizationCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<TInstanceNormalizationCS::FInstanceNormalizationAlgorithm>(DispatchAlgorithm);
			TShaderMapRef<TInstanceNormalizationCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
			FIntVector ThreadGroupCount = TInstanceNormalizationCS::GetGroupCount(*Parameters, DispatchAlgorithm);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.InstanceNormalization");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorInstanceNormalization);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.InstanceNormalization.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Parameters,
				ThreadGroupCount);
		}
	};

	bool ValidateInstanceNormalizationOperator(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<NNX::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddRequired(TEXT("epsilon"), ENNEAttributeDataType::Float);
		AttributeValidator.AddOptional(TEXT("__UE__algorithm"), ENNEAttributeDataType::String);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(EMLTensorDataType::Float);
		InputValidator.AddRequired();
		InputValidator.AddRequired();
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	NNX::FMLOperatorHlsl* CreateInstanceNormalizationOperator()
	{
		return new TInstanceNormalization();
	}

	bool RegisterInstanceNormalizationOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("InstanceNormalization"), CreateInstanceNormalizationOperator, ValidateInstanceNormalizationOperator);
		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
