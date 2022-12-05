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

	public:

		virtual int ComputeOutputShape(TConstArrayView<NNX::FTensorShape> InputShapes, TArray<NNX::FTensorShape>& OutputShapes) const override
		{
			OutputShapes.Empty();
			check(InputShapes.Num() == 2);

			const NNX::FTensorShape& InputA = InputShapes[0];
			const NNX::FTensorShape& InputB = InputShapes[1];

			if (InputA.Rank() < 2)
			{
				UE_LOG(LogNNX, Warning, TEXT("Matmul first input should be at least of rank 2"));
				return -1;
			}
			if (InputB.Rank() < 2)
			{
				UE_LOG(LogNNX, Warning, TEXT("Matmul second input should be at least of rank 2"));
				return -1;
			}
			if (InputA.Data[InputA.Rank() - 1] != InputB.Data[InputB.Rank() - 2])
			{
				UE_LOG(LogNNX, Warning, TEXT("Matmul first input last dimension should be equal to second input last dimension"));
				return -1;
			}

			const int32 OutputRank = FMath::Max(InputA.Rank(), InputB.Rank());
			NNX::FTensorShape OutputShape;
			
			OutputShape.Data.SetNumUninitialized(OutputRank);
			
			//Broadcast
			for (int32 i = 0; i < OutputRank; ++i)
			{
				int32 AIndex = InputA.Rank() - 1 - i;
				int32 BIndex = InputB.Rank() - 1 - i;
				int32 AValue = AIndex >= 0 ? InputA.Data[AIndex] : 1;
				int32 BValue = BIndex >= 0 ? InputB.Data[BIndex] : 1;
				int32 OutputValue = FMath::Max(AValue, BValue);
				OutputShape.Data[OutputRank - 1 - i] = OutputValue;
			}
			
			//2D Mat
			OutputShape.Data[OutputRank - 2] = InputA.Data[InputA.Rank() - 2];
			OutputShape.Data[OutputRank - 1] = InputB.Data[InputB.Rank() - 1];

			OutputShapes.Emplace(OutputShape);

			return 0;
		};

		virtual bool Initialize(TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensorDescs.Num() == 2);
			check(OutputTensorDescs.Num() == 1);

			const NNX::FTensorDesc& InputA = InputTensorDescs[0];
			const NNX::FTensorDesc& InputB = InputTensorDescs[1];

			if (InputA.GetShape().Rank() < 2)
			{
				UE_LOG(LogNNX, Warning, TEXT("Matmul first input should be at least of rank 2"));
				return false;
			}
			if (InputB.GetShape().Rank() < 2)
			{
				UE_LOG(LogNNX, Warning, TEXT("Matmul second input should be at least of rank 2"));
				return false;
			}

			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FTensorRDGRef> InputTensors, TConstArrayView<NNX::FTensorRDGRef> OutputTensors) override
		{
			using namespace UE::NNEHlslShaders::Internal;

			check(InputTensors.Num() == 2);
			check(OutputTensors.Num() == 1);
			check(InputTensors[0] != nullptr);
			check(InputTensors[1] != nullptr);
			check(OutputTensors[0] != nullptr);
			const NNX::FTensorRDG& InputA = *InputTensors[0];
			const NNX::FTensorRDG& InputB = *InputTensors[1];
			const NNX::FTensorRDG& Output = *OutputTensors[0];

			const EGemmAlgorithm Algorithm = EGemmAlgorithm::Simple32x32;

			const int32 NumStackDimensions = FMath::Max(FMath::Max(InputA.GetShape().Rank(), InputB.GetShape().Rank()) - 2, 0);

			// Set parameters
			TGemmCS::FParameters* Parameters = GraphBuilder.AllocParameters<TGemmCS::FParameters>();
			TGemmCS::FillInParametersMatMul(InputA, InputB, *Parameters);
			Parameters->A = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InputA.GetBuffer(), PF_R32_FLOAT));
			Parameters->B = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InputB.GetBuffer(), PF_R32_FLOAT));
			Parameters->Y = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.GetBuffer(), PF_R32_FLOAT));

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

	bool ValidateMatMulOperator(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<NNX::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(EMLTensorDataType::Float);
		InputValidator.AddRequired();
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	NNX::FMLOperatorHlsl* CreateMatMulOperator()
	{
		return new TMatMul();
	}

	bool RegisterMatMulOperator(NNX::FMLOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("MatMul"), CreateMatMulOperator, ValidateMatMulOperator);
		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
