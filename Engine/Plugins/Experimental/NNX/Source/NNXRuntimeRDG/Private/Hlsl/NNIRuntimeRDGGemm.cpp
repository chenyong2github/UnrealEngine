// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGGemm.h"
#include "NNEHlslShadersGemmCS.h"
#include "NNXRuntimeHLSLHelper.h"
#include "NNECoreAttributeMap.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorGemm, TEXT("NNI.Operator.Hlsl.Gemm"));

	/**
	 * Gemm operator implementation
	 */
	class TGemm : public NNX::FMLOperatorHlsl
	{
	public:

		TGemm() {}
		virtual ~TGemm() = default;

	private:

		NNX::FTensor InputA = {};
		NNX::FTensor InputB = {};
		NNX::FTensor InputC = {};
		NNX::FTensor Output = {};

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

		virtual bool Initialize(TArrayView<const NNX::FTensor> InputTensors, TArrayView<const NNX::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensors.Num() >= 2 && InputTensors.Num() <= 3);
			check(OutputTensors.Num() == 1);

			InputA = InputTensors[0];
			InputB = InputTensors[1];
			if (InputTensors.Num() == 3) InputC = InputTensors[2];
			Output = OutputTensors[0];

			check(InputA.GetShape().Rank() == 2);
			check(InputB.GetShape().Rank() == 2);
			check(InputC.GetShape().Rank() < 3);

			check(InputC.GetShape().Rank() != 1 || InputC.GetShape().Data[0] != 1); // TODO scalar version not supported yet

			// C is treated as a scalar if there is no valid C, either width or height is zero or C dimension is 1x1
			bIsCScalar = false; // InputTensors.Num() != 3 || InputC.Sizes[0] * InputC.Sizes[1] < 2;
			// CScalar = C != nullptr ? C[0] : (InElementType)0;
			bNoBias = InputTensors.Num() != 3 /*|| InputC.Sizes[0] * InputC.Sizes[1] < 1*/;

			InputAlpha = Attributes.GetValueOrDefault(TEXT("alpha"), InputAlpha);
			InputBeta = Attributes.GetValueOrDefault(TEXT("beta"), InputBeta);
			InputTransA = Attributes.GetValueOrDefault(TEXT("transA"), InputTransA);
			InputTransB = Attributes.GetValueOrDefault(TEXT("transB"), InputTransB);

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TArrayView<const NNX::FMLTensorBinding> InInputBindings, TArrayView<const NNX::FMLTensorBinding> OutOutputBindings) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			const EGemmAlgorithm Algorithm = EGemmAlgorithm::Simple32x32;

			const float CScalar = 0.0f;

			// Set parameters
			TGemmCS::FParameters* Parameters = GraphBuilder.AllocParameters<TGemmCS::FParameters>();
			TGemmCS::FillInParameters(InputAlpha, InputBeta, InputTransA, InputTransB, InputA, InputB, InputC, CScalar, *Parameters);
			Parameters->A = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[0].Buffer, PF_R32_FLOAT));
			Parameters->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[1].Buffer, PF_R32_FLOAT));
			if (InInputBindings.Num() == 3) {
				Parameters->C = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputBindings[2].Buffer, PF_R32_FLOAT));
			}
			Parameters->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(OutOutputBindings[0].Buffer, PF_R32_FLOAT));

			TGemmCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<TGemmCS::FGemmCScalar>(bNoBias ? EGemmCScalar::NoBias : (bIsCScalar ? EGemmCScalar::Yes : EGemmCScalar::No));
			PermutationVector.Set<TGemmCS::FGemmAlgorithm>(Algorithm);
			PermutationVector.Set<TGemmCS::FGemmNumStackDimensions>(0);
			TShaderMapRef<TGemmCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			FIntVector ThreadGroupCount = TGemmCS::GetGroupCount(*Parameters, Algorithm, 0);

			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.Gemm");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorGemm);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Parameters,
				ThreadGroupCount);
		}
	};

	bool ValidateGemmOperator(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<NNX::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("alpha"), ENNEAttributeDataType::Float);
		AttributeValidator.AddOptional(TEXT("beta"), ENNEAttributeDataType::Float);
		AttributeValidator.AddOptional(TEXT("transA"), ENNEAttributeDataType::Int32);
		AttributeValidator.AddOptional(TEXT("transB"), ENNEAttributeDataType::Int32);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(EMLTensorDataType::Float);
		InputValidator.AddRequired();
		InputValidator.AddRequired();
		InputValidator.AddOptional();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	NNX::FMLOperatorHlsl* CreateGemmOperator()
	{
		return new TGemm();
	}

	bool RegisterGemmOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Gemm"), CreateGemmOperator, ValidateGemmOperator);
		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
