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

		NNX::FMLTensorDesc Input = {};
		NNX::FMLTensorDesc Weights = {};
		NNX::FMLTensorDesc Bias = {};
		NNX::FMLTensorDesc Output = {};

		int NumDimensions = 0;
		bool HasBias = false;

		// Hard coded parameters, until we accept them from JSON
		int32 Group = 1;
		EConvAutoPad AutoPad = EConvAutoPad::VALID;
		TArray<int32> Dilations = {1};
		TArray<int32> Strides = {1};
		TArray<int32> Pads = {0, 0};

	public:

		virtual bool Initialize(TArrayView<const NNX::FMLTensorDesc> InputTensors, TArrayView<const NNX::FMLTensorDesc> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensors.Num() >= 2 && InputTensors.Num() <= 3);
			check(OutputTensors.Num() == 1);

			Input = InputTensors[0];
			Weights = InputTensors[1];
			Output = OutputTensors[0];

			check(Input.Shape.Num() > 2);
			check(Weights.Shape.Num()  == Input.Shape.Num());
			check(Output.Shape.Num() == Input.Shape.Num());

			if (InputTensors.Num() == 3) {
				HasBias = true;
				Bias = InputTensors[2];
			}
			else {
				HasBias = false;
			}

			NumDimensions = Input.Shape.Num() - 2;

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			constexpr EConvAlgorithm Algorithm = EConvAlgorithm::SharedMemory;
			constexpr EConvGroupSize GroupSize = EConvGroupSize::Size256;

			TArray<int32> OutputShape = FConvCS::GetOutputShape(Input.Shape.Data, Weights.Shape.Data, AutoPad, Dilations, Strides, Pads);

			// Set parameters
			FConvCS::FParameters* Params = GraphBuilder.AllocParameters<FConvCS::FParameters>();
			FConvCS::FillInParameters(GroupSize, Input.Shape.Data, Weights.Shape.Data, HasBias, AutoPad, Group, Dilations,Strides, Pads, *Params);
			Params->X = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Params->W = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			if (InInputBindings.Num() == 3) {
				Params->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[2].Buffer, PF_R32_FLOAT));
			}
			Params->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			FConvCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<FConvCS::FConvAlgorithm>(Algorithm);
			PermutationVector.Set<FConvCS::FConvGroupSize>(GroupSize);
			PermutationVector.Set<FConvCS::FConvNumDimensions>(NumDimensions);
			PermutationVector.Set<FConvCS::FConvNumReadsPerThread>(FConvCS::GetNumReadsPerThread(GroupSize, Weights.Shape.Data, Dilations, Strides));
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
