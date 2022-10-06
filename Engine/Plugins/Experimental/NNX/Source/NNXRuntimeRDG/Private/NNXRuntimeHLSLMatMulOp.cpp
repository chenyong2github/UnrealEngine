// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLMatMulOp.h"
#include "NNXGemmCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace NNX
{

	/**
	 * Gemm ML operator implementation
	 */
	class FMLOperatorHlslMatMul : public FMLOperatorHlsl
	{
	public:

		static FMLOperatorHlsl* Create()
		{
			return new FMLOperatorHlslMatMul();
		}

		virtual ~FMLOperatorHlslMatMul() = default;

	private:

		FMLOperatorHlslMatMul() {}

		FMLTensorDesc InputA = {};
		FMLTensorDesc InputB = {};
		FMLTensorDesc Output = {};

	public:

		virtual bool Initialize(TArrayView<const FMLTensorDesc> InputTensors, TArrayView<const FMLTensorDesc> OutputTensors) override
		{
			check(InputTensors.Num() == 2);
			check(OutputTensors.Num() == 1);

			InputA = InputTensors[0];
			InputB = InputTensors[1];
			Output = OutputTensors[0];

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override
		{
			const EGemmAlgorithm Algorithm = EGemmAlgorithm::Simple32x32;

			const int32 NumStackDimensions = FMath::Max((int32)FMath::Max(InputA.Dimension, InputB.Dimension) - 2, 0);

			// Set parameters
			FMLGemmCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMLGemmCS::FParameters>();
			FMLGemmCS::FillInParametersMatMul(InputA, InputB, *Parameters);
			Parameters->A = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Parameters->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			Parameters->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			FMLGemmCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FMLGemmCS::FGemmCScalar>(EGemmCScalar::NoBias);
			PermutationVector.Set<FMLGemmCS::FGemmAlgorithm>(Algorithm);
			PermutationVector.Set<FMLGemmCS::FGemmNumStackDimensions>(NumStackDimensions);
			TShaderMapRef<FMLGemmCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FIntVector ThreadGroupCount = FMLGemmCS::GetGroupCount(*Parameters, Algorithm, NumStackDimensions);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("FMLHlslMatMulOperatorHlsl_Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Parameters,
				ThreadGroupCount);
		}
	};

	bool RegisterMatMulOperator(FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("MatMul"), FMLOperatorHlslMatMul::Create);

		return true;
	}

} // NNX
