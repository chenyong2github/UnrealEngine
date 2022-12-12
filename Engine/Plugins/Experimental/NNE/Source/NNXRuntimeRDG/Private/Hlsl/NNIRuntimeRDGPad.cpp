// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGPad.h"
#include "NNEHlslShadersPadCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorPad, TEXT("NNI.Operator.Hlsl.Pad"));

	using EPadMode = UE::NNEHlslShaders::Internal::EPadMode;

	/**
	 * Pad operator implementation
	 */
	class FPad : public NNX::FMLOperatorHlsl
	{
	public:

		FPad() {}
		virtual ~FPad() = default;

		TArray<int32> Pads;
		float Value;
		EPadMode Mode;

	public:

		virtual int PrepareOutputs(TConstArrayView<NNX::FTensorRef> InputTensors, TArrayView<NNX::FTensorRef> OutputTensors) const override
		{
			check(InputTensors.Num() == 1);
			check(OutputTensors.Num() == 1);

			const NNX::FTensor& X = *InputTensors[0];

			NNX::FTensorShape OutputShape;
			for (int32 i = 0; i < X.GetShape().Rank(); ++i)
			{
				int32 PrePad = Pads[i];
				int32 PostPad = Pads[i + X.GetShape().Rank()];
				int32 OutputDim = PrePad + X.GetShape().Data[i] + PostPad;
				if (OutputDim < 1)
				{
					UE_LOG(LogNNX, Warning, TEXT("Pads cannot reduce dimension below 1, but would for tensor (name:%s) at rank %d of size %d with prepad %d and postpad %d."), *X.GetName(), i, X.GetShape().Data[i], PrePad, PostPad);
					return -1;
				}
				OutputShape.Data.Emplace(OutputDim);
			}

			OutputTensors[0]->SetShape(OutputShape);

			return 0;
		};

		virtual bool Initialize(TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) override
		{
			using namespace UE::NNEHlslShaders::Internal;
			
			check(InputTensorDescs.Num() == 1);
			check(OutputTensorDescs.Num() == 1);
			
			Pads = Attributes.GetValue<TArray<int32>>(TEXT("pads"));
			Value = Attributes.GetValueOrDefault<float>(TEXT("value"), 0.0f);
			FPadCS::LexFromString(Mode, *Attributes.GetValue<FString>(TEXT("mode")));

			if ((2*InputTensorDescs[0].GetShape().Rank()) != Pads.Num())
			{
				UE_LOG(LogNNX, Warning, TEXT("pads attribute lenght (%d) should be twice the rank of input X (%d)."), Pads.Num(), InputTensorDescs[0].GetShape().Rank());
				return false;
			}

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FTensorRDGRef> InputTensors, TConstArrayView<NNX::FTensorRDGRef> OutputTensors) override
		{
			using namespace UE::NNEHlslShaders::Internal;
			
			check(InputTensors.Num() == 1);
			check(OutputTensors.Num() == 1);
			check(InputTensors[0] != nullptr);
			check(OutputTensors[0] != nullptr);
			const NNX::FTensorRDG& Input = *InputTensors[0];
			const NNX::FTensorRDG& Output = *OutputTensors[0];

			FRDGBufferSRVRef InputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(Input.GetBuffer(), PF_R32_FLOAT));
			FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.GetBuffer(), PF_R32_FLOAT));

			FIntVector ThreadGroupCount = NNX::ComputeElementWiseThreadGroups(Output.GetVolume(), FPadConstants::NUM_GROUP_THREADS);

			// Set parameters
			FPadCS::FParameters* Params = GraphBuilder.AllocParameters<FPadCS::FParameters>();
			Params->Input = InputSRV;
			Params->Output = OutputUAV;
			FillTensorStrideShaderParameters(Input, Params->TensorInfo, 0);
			FillTensorStrideShaderParameters(Output, Params->TensorInfo, 1);
			FillTensorSizeShaderParameters(Input, Params->TensorInfo, 2);
			for (int32 i = 0; i < Input.GetShape().Rank(); ++i)
			{
				Params->TensorInfo[i][3] = Pads[i];//Pre-pad
			}
			Params->Value = Value;
			Params->Num = Output.GetVolume();
			Params->ThreadCountX = ThreadGroupCount.X * FPadConstants::NUM_GROUP_THREADS;

			FPadCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FPadCS::FPadMode>(Mode);
			PermutationVector.Set<FPadCS::FPadNumDimensions>(Output.GetShape().Rank());

			TShaderMapRef<FPadCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.Pad");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorPad);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.Pad.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				ThreadGroupCount);
		}
	};

	bool ValidatePadOperator(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<NNX::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		//This match version 2 of the pad operator (next version is with opset 11)
		//https://github.com/onnx/onnx/blob/main/docs/Changelog.md#Pad-2
		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("mode"), ENNEAttributeDataType::String);
		AttributeValidator.AddRequired(TEXT("pads"), ENNEAttributeDataType::Int32Array);
		AttributeValidator.AddOptional(TEXT("value"), ENNEAttributeDataType::Float);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		for (int32 Pad : AttributeMap.GetValue<TArray<int32>>(TEXT("pads")))
		{
			if (Pad < 0)
			{
				UE_LOG(LogNNX, Warning, TEXT("Pad operator does not support negative padding at the moment."));
				return false;
			}
		}
		
		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(EMLTensorDataType::Float);
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	NNX::FMLOperatorHlsl* CreatePadOperator()
	{
		return new FPad();
	}

	bool RegisterPadOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Pad"), CreatePadOperator, ValidatePadOperator);
		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
