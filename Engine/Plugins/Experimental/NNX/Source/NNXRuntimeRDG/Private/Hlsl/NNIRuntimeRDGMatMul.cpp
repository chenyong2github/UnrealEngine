// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGMatMul.h"
#include "NNEHlslShadersGemmCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
    DECLARE_GPU_STAT_NAMED(FNNIOperatorMatMul, TEXT("NNI.Operator.Hlsl.MatMul"));

	/**
	 * MatMul operator implementation
	 */
	class TMatMul : public NNX::FMLOperatorHlsl
	{
	public:

		TMatMul() {}
		virtual ~TMatMul() = default;

	private:

		NNX::FTensor InputA = {};
		NNX::FTensor InputB = {};
		NNX::FTensor Output = {};

	public:

		virtual bool Initialize(TArrayView<const NNX::FTensor> InputTensors, TArrayView<const NNX::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensors.Num() == 2);
			check(OutputTensors.Num() == 1);

			InputA = InputTensors[0];
			InputB = InputTensors[1];
			Output = OutputTensors[0];

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			const EGemmAlgorithm Algorithm = EGemmAlgorithm::Simple32x32;

			const int32 NumStackDimensions = FMath::Max(FMath::Max(InputA.GetShape().Rank(), InputB.GetShape().Rank()) - 2, 0);

			// Set parameters
			TGemmCS::FParameters* Parameters = GraphBuilder.AllocParameters<TGemmCS::FParameters>();
			TGemmCS::FillInParametersMatMul(InputA, InputB, *Parameters);
			Parameters->A = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Parameters->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			Parameters->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			TGemmCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<TGemmCS::FGemmCScalar>(EGemmCScalar::NoBias);
			PermutationVector.Set<TGemmCS::FGemmAlgorithm>(Algorithm);
			PermutationVector.Set<TGemmCS::FGemmNumStackDimensions>(NumStackDimensions);
			TShaderMapRef<TGemmCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FIntVector ThreadGroupCount = TGemmCS::GetGroupCount(*Parameters, Algorithm, NumStackDimensions);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.MatMul");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorMatMul);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.MatMul.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Parameters,
				ThreadGroupCount);
		}
	};

	NNX::FMLOperatorHlsl* CreateMatMulOperator()
	{
		return new TMatMul();
	}

	bool RegisterMatMulOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("MatMul"), CreateMatMulOperator);
		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
