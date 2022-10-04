// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeHLSLGemmOp.h"
#include "NNXGemmCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace NNX
{

	/**
	 * Gemm ML operator implementation
	 */
	class FMLOperatorHlslGemm : public FMLOperatorHlsl
	{
	public:

		static FMLOperatorHlsl* Create()
		{
			return new FMLOperatorHlslGemm();
		}

		virtual ~FMLOperatorHlslGemm() = default;

	private:

		FMLOperatorHlslGemm() {}

		FMLTensorDesc InputA = {};
		FMLTensorDesc InputB = {};
		FMLTensorDesc InputC = {};
		FMLTensorDesc Output = {};

		float InputAlpha = 1.0f;
		float InputBeta = 1.0f;
		int32 InputTransA = 0;
		int32 InputTransB = 0;
		uint32 InputM = 0;
		uint32 InputN = 0;
		uint32 InputK = 0;
		uint32 InputCWidth = 0;
		uint32 InputCHeight = 0;

		bool bIsCScalar = false;
		bool bNoBias = true;

	public:

		virtual bool Initialize(TArrayView<const FMLTensorDesc> InputTensors, TArrayView<const FMLTensorDesc> OutputTensors) override
		{
			check(InputTensors.Num() >= 2 && InputTensors.Num() <= 3);
			check(OutputTensors.Num() == 1);

			InputA = InputTensors[0];
			InputB = InputTensors[1];
			if (InputTensors.Num() == 3) InputC = InputTensors[2];
			Output = OutputTensors[0];

			check(InputA.Dimension == 2);
			check(InputB.Dimension == 2);
			check(InputC.Dimension < 3);

			check(InputC.Dimension != 1 || InputC.Sizes[0] != 1); // TODO scalar version not supported yet

			// C is treated as a scalar if there is no valid C, either width or height is zero or C dimension is 1x1
			bIsCScalar = false; // InputTensors.Num() != 3 || InputC.Sizes[0] * InputC.Sizes[1] < 2;
			// CScalar = C != nullptr ? C[0] : (InElementType)0;
			bNoBias = InputTensors.Num() != 3 /*|| InputC.Sizes[0] * InputC.Sizes[1] < 1*/;

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings) override
		{
			const EGemmAlgorithm Algorithm = EGemmAlgorithm::Simple32x32;

			const float CScalar = 0.0f;

			// Set parameters
			FMLGemmCS::FParameters* Parameters = GraphBuilder.AllocParameters<FMLGemmCS::FParameters>();
			FMLGemmCS::FillInParameters(InputAlpha, InputBeta, InputTransA, InputTransB, InputA, InputB, InputC, CScalar, *Parameters);
			Parameters->A = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Parameters->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			if (InInputBindings.Num() == 3) {
				Parameters->C = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[2].Buffer, PF_R32_FLOAT));
			}
			Parameters->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			FMLGemmCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FMLGemmCS::FGemmCScalar>(bNoBias ? EGemmCScalar::NoBias : (bIsCScalar ? EGemmCScalar::Yes : EGemmCScalar::No));
			PermutationVector.Set<FMLGemmCS::FGemmAlgorithm>(Algorithm);
			PermutationVector.Set<FMLGemmCS::FGemmNumStackDimensions>(0);
			TShaderMapRef<FMLGemmCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FIntVector ThreadGroupCount = FMLGemmCS::GetGroupCount(*Parameters, Algorithm, 0);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("FMLHlslGemmOperatorHlsl_Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Parameters,
				ThreadGroupCount);
		}
	};

	bool RegisterGemmOperator(FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Gemm"), FMLOperatorHlslGemm::Create);

		return true;
	}

} // NNX
