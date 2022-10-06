// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLConvTransposeOp.h"
#include "NNXConvTransposeCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace NNX
{

	/**
	 * ConvTranspose ML operator implementation
	 */
	class FMLOperatorHlslConvTranspose : public FMLOperatorHlsl
	{
	public:

		static FMLOperatorHlsl* Create()
		{
			return new FMLOperatorHlslConvTranspose();
		}

		virtual ~FMLOperatorHlslConvTranspose() = default;

	private:

		FMLOperatorHlslConvTranspose(){}

		FMLTensorDesc Input;
		FMLTensorDesc Weights;
		FMLTensorDesc Bias;
		FMLTensorDesc Output;

		int NumDimensions;
		bool HasBias;

		// Hard coded parameters, until we accept them from JSON
		int32 Group = 1;
		TArray<int32> InputShape;
		TArray<int32> WeightsShape;
		EConvTransposeAutoPad AutoPad = EConvTransposeAutoPad::VALID;
		TArray<int32> Dilations = {1};
		TArray<int32> Strides = {1};
		TArray<int32> Pads = {0, 0};
		TArray<int32> OutputPadding = {0};

	public:

		virtual bool Initialize(TArrayView<const FMLTensorDesc> InputTensors, TArrayView<const FMLTensorDesc> OutputTensors) override
		{
			check(InputTensors.Num() >= 2 && InputTensors.Num() <= 3);
			check(OutputTensors.Num() == 1);

			Input = InputTensors[0];
			Weights = InputTensors[1];
			Output = OutputTensors[0];

			check(Input.Dimension > 2);
			check(Weights.Dimension  == Input.Dimension);
			check(Output.Dimension == Input.Dimension);

			if (InputTensors.Num() == 3) {
				HasBias = true;
				Bias = InputTensors[2];
			}
			else {
				HasBias = false;
			}

			auto MakeShape = [](const NNX::FMLTensorDesc& InputDesc) {
				TArray<int32> Result;
				for (int32 i = 0; i < (int32)InputDesc.Dimension; i++) {
					Result.Add(InputDesc.Sizes[i]);
				}
				return Result;
			};
			InputShape = MakeShape(Input);
			WeightsShape = MakeShape(Weights);

			NumDimensions = Input.Dimension - 2;

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override
		{
			constexpr EConvTransposeAlgorithm Algorithm = EConvTransposeAlgorithm::SharedMemory;
			constexpr EConvTransposeGroupSize GroupSize = EConvTransposeGroupSize::Size256;

			TArray<int32> OutputShape = FMLConvTransposeCS::GetOutputShape(InputShape, WeightsShape, AutoPad, Dilations, Strides, Pads, OutputPadding, Group);

			// Set parameters
			FMLConvTransposeCS::FParameters* Params = GraphBuilder.AllocParameters<FMLConvTransposeCS::FParameters>();
			FMLConvTransposeCS::FillInParameters(GroupSize, InputShape, WeightsShape, HasBias, AutoPad, Group, Dilations,Strides, Pads, OutputPadding, *Params);
			Params->X = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Params->W = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			if (InInputBindings.Num() == 3) {
				Params->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[2].Buffer, PF_R32_FLOAT));
			}
			Params->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			FMLConvTransposeCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<FMLConvTransposeCS::FConvTransposeAlgorithm>(Algorithm);
			PermutationVector.Set<FMLConvTransposeCS::FConvTransposeGroupSize>(GroupSize);
			PermutationVector.Set<FMLConvTransposeCS::FConvTransposeNumStackDimensions>(NumDimensions);
			PermutationVector.Set<FMLConvTransposeCS::FConvTransposeNumReadsPerThread>(FMLConvTransposeCS::GetNumReadsPerThread(GroupSize, WeightsShape, Dilations, Strides));
			PermutationVector.Set<FMLConvTransposeCS::FConvTransposeHasB>(HasBias);
			TShaderMapRef<FMLConvTransposeCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("FMLOperatorHlslConvTranspose_Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				FMLConvTransposeCS::GetGroupCount(OutputShape, FMLConvTransposeCS::GetGroupShape(GroupSize, NumDimensions)));		
		}
	};

	bool RegisterConvTransposeOperator(FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("ConvTranspose"), FMLOperatorHlslConvTranspose::Create);

		return true;
	}

} // NNX
