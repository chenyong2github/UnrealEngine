// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGConvTranspose.h"
#include "NNEHlslShadersConvTransposeCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorConvTranspose, TEXT("NNI.Operator.Hlsl.ConvTranspose"));

	using EConvTransposeAutoPad = UE::NNEHlslShaders::Internal::EConvTransposeAutoPad;

	/**
	 * ConvTranspose operator implementation
	 */
	class FConvTranspose : public NNX::FMLOperatorHlsl
	{
	public:

		static NNX::FMLOperatorHlsl* Create()
		{
			return new FConvTranspose();
		}

		virtual ~FConvTranspose() = default;

	private:

		FConvTranspose() {}

		NNX::FTensor Input = {};
		NNX::FTensor Weights = {};
		NNX::FTensor Bias = {};
		NNX::FTensor Output = {};

		int NumDimensions = 0;
		bool HasBias = false;

		EConvTransposeAutoPad AutoPad = EConvTransposeAutoPad::NOTSET;
		TArray<int32> Dilations;
		int32 Group = 1;
		TArray<int32> OutputPadding;
		TArray<int32> Pads;
		TArray<int32> Strides;

	public:

		virtual bool Initialize(TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			TArray<NNX::FTensor> InputTensors;
			TArray<NNX::FTensor> OutputTensors;
			if (!NNX::ConvertConcreteTensorDescsToTensors(InputTensorDescs, InputTensors) ||
				!NNX::ConvertConcreteTensorDescsToTensors(OutputTensorDescs, OutputTensors))
			{
				UE_LOG(LogNNX, Warning, TEXT("Variable input shapes are not supported by this operator"));
				return false;
			}
				
			check(InputTensors.Num() >= 2 && InputTensors.Num() <= 3);
			check(OutputTensors.Num() == 1);

			Input = InputTensors[0];
			Weights = InputTensors[1];
			Output = OutputTensors[0];

			check(Input.GetShape().Rank() > 2);
			check(Weights.GetShape().Rank()  == Input.GetShape().Rank());
			check(Output.GetShape().Rank() == Input.GetShape().Rank());

			if (InputTensors.Num() == 3) {
				HasBias = true;
				Bias = InputTensors[2];
			}
			else
			{
				HasBias = false;
			}

			NumDimensions = Input.GetShape().Rank() - 2;

			TArray<int32> DilationsOrStridesDefault;
			DilationsOrStridesDefault.Init(1, NumDimensions);

			FConvTransposeCS::LexFromString(AutoPad, *Attributes.GetValue<FString>(TEXT("auto_pad")));
			Dilations = Attributes.GetValueOrDefault<TArray<int32>>(TEXT("dilations"), DilationsOrStridesDefault);
			Group = Attributes.GetValueOrDefault<int>(TEXT("group"), 1);
			
			TArray<int32> OutputPaddingDefault;
			OutputPaddingDefault.Init(0, NumDimensions);
			OutputPadding = Attributes.GetValueOrDefault<TArray<int32>>(TEXT("output_padding"), OutputPaddingDefault);
			
			if (AutoPad == EConvTransposeAutoPad::NOTSET)
			{
				TArray<int32> PadsDefault;
				PadsDefault.Init(1, 2 * NumDimensions);

				Pads = Attributes.GetValueOrDefault<TArray<int32>>(TEXT("pads"), PadsDefault);
			}
			Strides = Attributes.GetValueOrDefault<TArray<int32>>(TEXT("strides"), DilationsOrStridesDefault);

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FTensorRDG> InInputTensors, TConstArrayView<NNX::FTensorRDG> InOutputTensors) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			constexpr EConvTransposeAlgorithm Algorithm = EConvTransposeAlgorithm::SharedMemory;
			constexpr EConvTransposeGroupSize GroupSize = EConvTransposeGroupSize::Size256;

			TArray<int32> OutputShape = FConvTransposeCS::GetOutputShape(Input.GetShape().Data, Weights.GetShape().Data, AutoPad, Dilations, Strides, Pads, OutputPadding, Group);

			// Set parameters
			FConvTransposeCS::FParameters* Params = GraphBuilder.AllocParameters<FConvTransposeCS::FParameters>();
			FConvTransposeCS::FillInParameters(GroupSize, Input.GetShape().Data, Weights.GetShape().Data, HasBias, AutoPad, Group, Dilations,Strides, Pads, OutputPadding, *Params);
			Params->X = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputTensors[0].GetBuffer(), PF_R32_FLOAT));
			Params->W = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputTensors[1].GetBuffer(), PF_R32_FLOAT));
			if (InInputTensors.Num() == 3) {
				Params->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputTensors[2].GetBuffer(), PF_R32_FLOAT));
			}
			Params->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(InOutputTensors[0].GetBuffer(), PF_R32_FLOAT));

			FConvTransposeCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<FConvTransposeCS::FConvTransposeAlgorithm>(Algorithm);
			PermutationVector.Set<FConvTransposeCS::FConvTransposeGroupSize>(GroupSize);
			PermutationVector.Set<FConvTransposeCS::FConvTransposeNumStackDimensions>(NumDimensions);
			PermutationVector.Set<FConvTransposeCS::FConvTransposeNumReadsPerThread>(FConvTransposeCS::GetNumReadsPerThread(GroupSize, Weights.GetShape().Data, Dilations, Strides));
			PermutationVector.Set<FConvTransposeCS::FConvTransposeHasB>(HasBias);
			TShaderMapRef<FConvTransposeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.ConvTranspose");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorConvTranspose);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.ConvTranspose.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				FConvTransposeCS::GetGroupCount(OutputShape, FConvTransposeCS::GetGroupShape(GroupSize, NumDimensions)));		
		}
	};

	bool RegisterConvTransposeOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("ConvTranspose"), FConvTranspose::Create);

		return true;
	}

} // UE::NNIRuntimeRDG::Private::Hlsl
