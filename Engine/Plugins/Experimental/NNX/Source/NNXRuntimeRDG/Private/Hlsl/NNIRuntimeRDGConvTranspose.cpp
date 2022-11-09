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

		// Hard coded parameters, until we accept them from JSON
		int32 Group = 1;
		EConvTransposeAutoPad AutoPad = EConvTransposeAutoPad::VALID;
		TArray<int32> Dilations = {1};
		TArray<int32> Strides = {1};
		TArray<int32> Pads = {0, 0};
		TArray<int32> OutputPadding = {0};

	public:

		virtual bool Initialize(TArrayView<const NNX::FTensor> InputTensors, TArrayView<const NNX::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
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

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			constexpr EConvTransposeAlgorithm Algorithm = EConvTransposeAlgorithm::SharedMemory;
			constexpr EConvTransposeGroupSize GroupSize = EConvTransposeGroupSize::Size256;

			TArray<int32> OutputShape = FConvTransposeCS::GetOutputShape(Input.GetShape().Data, Weights.GetShape().Data, AutoPad, Dilations, Strides, Pads, OutputPadding, Group);

			// Set parameters
			FConvTransposeCS::FParameters* Params = GraphBuilder.AllocParameters<FConvTransposeCS::FParameters>();
			FConvTransposeCS::FillInParameters(GroupSize, Input.GetShape().Data, Weights.GetShape().Data, HasBias, AutoPad, Group, Dilations,Strides, Pads, OutputPadding, *Params);
			Params->X = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Params->W = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			if (InInputBindings.Num() == 3) {
				Params->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[2].Buffer, PF_R32_FLOAT));
			}
			Params->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

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
