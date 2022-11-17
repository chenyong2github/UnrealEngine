// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGConv.h"
#include "NNEHlslShadersConvCS.h"
#include "NNECoreAttributeMap.h"
#include "NNXRuntimeHLSLHelper.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorConv, TEXT("NNI.Operator.Hlsl.Conv"));

	using EConvAutoPad = UE::NNEHlslShaders::Internal::EConvAutoPad;

	/**
	 * Convolution operator implementation
	 */
	class FConv : public NNX::FMLOperatorHlsl
	{
	public:

		static NNX::FMLOperatorHlsl* Create()
		{
			return new FConv();
		}

		virtual ~FConv() = default;

	private:

		FConv() {}

		NNX::FTensor Input = {};
		NNX::FTensor Weights = {};
		NNX::FTensor Bias = {};
		NNX::FTensor Output = {};

		int NumDimensions = 0;
		bool HasBias = false;

		EConvAutoPad AutoPad = EConvAutoPad::NOTSET;
		TArray<int32> Dilations;
		int32 Group = 1;
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
			else {
				HasBias = false;
			}

			NumDimensions = Input.GetShape().Rank() - 2;

			TArray<int32> DilationsOrStridesDefault;
			DilationsOrStridesDefault.Init(1, NumDimensions);

			FConvCS::LexFromString(AutoPad, *Attributes.GetValue<FString>(TEXT("auto_pad")));
			Dilations = Attributes.GetValueOrDefault<TArray<int32>>(TEXT("dilations"), DilationsOrStridesDefault);
			Group = Attributes.GetValueOrDefault<int>(TEXT("group"), 1);
			if (AutoPad == EConvAutoPad::NOTSET)
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

			constexpr EConvAlgorithm Algorithm = EConvAlgorithm::SharedMemory;
			constexpr EConvGroupSize GroupSize = EConvGroupSize::Size256;

			TArray<int32> OutputShape = FConvCS::GetOutputShape(Input.GetShape().Data, Weights.GetShape().Data, AutoPad, Dilations, Strides, Pads);

			// Set parameters
			FConvCS::FParameters* Params = GraphBuilder.AllocParameters<FConvCS::FParameters>();
			FConvCS::FillInParameters(GroupSize, Input.GetShape().Data, Weights.GetShape().Data, HasBias, AutoPad, Group, Dilations,Strides, Pads, *Params);
			Params->X = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputTensors[0].GetBuffer(), PF_R32_FLOAT));
			Params->W = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputTensors[1].GetBuffer(), PF_R32_FLOAT));
			if (InInputTensors.Num() == 3) {
				Params->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputTensors[2].GetBuffer(), PF_R32_FLOAT));
			}
			Params->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(InOutputTensors[0].GetBuffer(), PF_R32_FLOAT));

			FConvCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<FConvCS::FConvAlgorithm>(Algorithm);
			PermutationVector.Set<FConvCS::FConvGroupSize>(GroupSize);
			PermutationVector.Set<FConvCS::FConvNumDimensions>(NumDimensions);
			PermutationVector.Set<FConvCS::FConvNumReadsPerThread>(FConvCS::GetNumReadsPerThread(GroupSize, Weights.GetShape().Data, Dilations, Strides));
			PermutationVector.Set<FConvCS::FConvHasB>(HasBias);
			TShaderMapRef<FConvCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.Conv");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorConv);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.Conv.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				FConvCS::GetGroupCount(OutputShape, FConvCS::GetGroupShape(GroupSize, NumDimensions)));
		}
	};

	bool RegisterConvOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Conv"), FConv::Create);

		return true;
	}

} // UE::NNIRuntimeRDG::Private::Hlsl
