// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGElementWiseBinary.h"
#include "NNEHlslShadersElementWiseBinaryCS.h"
#include "NNXRuntimeHLSLHelper.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNIOperatorElementWiseBinary, TEXT("NNI.Operator.Hlsl.ElementWise.Binary"));

	using TElementWiseBinaryCS = typename UE::NNEHlslShaders::Internal::TElementWiseBinaryCS;
	using FElementWiseBinaryConstants = UE::NNEHlslShaders::Internal::FElementWiseBinaryConstants;

	/**
	 * Binary element-wise operator implementation
	 */
	template<EMLElementWiseBinaryOperatorType OpType>
	class TElementWiseBinary : public NNX::FMLOperatorHlsl
	{
	public:

		TElementWiseBinary() {}
		virtual ~TElementWiseBinary() = default;

	public:

		virtual int PrepareOutputs(TConstArrayView<NNX::FTensorRef> InputTensors, TArrayView<NNX::FTensorRef> OutputTensors) const override
		{
			check(InputTensors.Num() == 2);
			check(OutputTensors.Num() == 1);
			
			const NNX::FTensorShape& LHSInput = InputTensors[0]->GetShape();
			const NNX::FTensorShape& RHSInput = InputTensors[1]->GetShape();
			const int32 OutputRank = FMath::Max(LHSInput.Rank(), RHSInput.Rank());
			NNX::FTensorShape OutputShape;
			
			OutputShape.Data.SetNumUninitialized(OutputRank);
			
			for (int32 i = 0; i < OutputRank; ++i)
			{
				int32 LHSIndex = LHSInput.Rank() - 1 - i;
				int32 RHSIndex = RHSInput.Rank() - 1 - i;
				int32 LHSValue = LHSIndex >= 0 ? LHSInput.Data[LHSIndex] : 1;
				int32 RHSValue = RHSIndex >= 0 ? RHSInput.Data[RHSIndex] : 1;
				if (LHSValue != RHSValue && LHSValue != 1 && RHSValue != 1)
				{
					UE_LOG(LogNNX, Warning, TEXT("Error while computing shape for element wise binary op, input shapes are not compatible"));
					return -1;
				}
				int32 OutputValue = FMath::Max(LHSValue, RHSValue);
				OutputShape.Data[OutputRank - 1 - i] = OutputValue;
			}

			OutputTensors[0]->SetShape(OutputShape);
			
			return 0;
		}
		
		virtual bool Initialize(TConstArrayView<NNX::FTensorDesc> InputTensorDescs, TConstArrayView<NNX::FTensorDesc> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensorDescs.Num() == 2);
			check(OutputTensorDescs.Num() == 1);
		
			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FTensorRDGRef> InputTensors, TConstArrayView<NNX::FTensorRDGRef> OutputTensors) override
		{
			check(InputTensors.Num() == 2);
			check(OutputTensors.Num() == 1);
			check(InputTensors[0] != nullptr);
			check(InputTensors[1] != nullptr);
			check(OutputTensors[0] != nullptr);
			const NNX::FTensorRDG& LHSInput = *InputTensors[0];
			const NNX::FTensorRDG& RHSInput = *InputTensors[1];
			const NNX::FTensorRDG& Output = *OutputTensors[0];

			FRDGBufferSRVRef LHSInputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(LHSInput.GetBuffer(), PF_R32_FLOAT));
			FRDGBufferSRVRef RHSInputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(RHSInput.GetBuffer(), PF_R32_FLOAT));
			FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(Output.GetBuffer(), PF_R32_FLOAT));

			FIntVector ThreadGroupCount = NNX::ComputeElementWiseThreadGroups(Output.GetVolume(), FElementWiseBinaryConstants::NUM_GROUP_THREADS);

			// Set parameters
			TElementWiseBinaryCS::FParameters* Params = GraphBuilder.AllocParameters<TElementWiseBinaryCS::FParameters>();
			Params->LHSInput = LHSInputSRV;
			Params->RHSInput = RHSInputSRV;
			Params->Output = OutputUAV;
			FillTensorStrideForBroadcastShaderParameters(LHSInput, Output.GetShape().Rank(), Params->TensorInfo, 0);
			FillTensorStrideForBroadcastShaderParameters(RHSInput, Output.GetShape().Rank(), Params->TensorInfo, 1);
			FillTensorStrideShaderParameters(Output, Params->TensorInfo, 2);
			Params->Num = Output.GetVolume();
			Params->ThreadCountX = ThreadGroupCount.X * FElementWiseBinaryConstants::NUM_GROUP_THREADS;

			TElementWiseBinaryCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<TElementWiseBinaryCS::FOperatorType>(OpType);
			PermutationVector.Set<TElementWiseBinaryCS::FBinaryNumDimensions>(Output.GetShape().Rank());

			TShaderMapRef<TElementWiseBinaryCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);
		
			RDG_EVENT_SCOPE(GraphBuilder, "NNI.Operator.Hlsl.ElementWise.Binary");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNIOperatorElementWiseBinary);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.ElementWise.Binary.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				ThreadGroupCount);
		}
	};

	bool ValidateElementWiseBinaryOperator(const UE::NNECore::FAttributeMap& AttributeMap, TConstArrayView<EMLTensorDataType> InputTypes, TConstArrayView<NNX::FSymbolicTensorShape> InputShapes)
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

	template<EMLElementWiseBinaryOperatorType OpType>
	NNX::FMLOperatorHlsl* CreateElementWiseBinaryOperator()
	{
		return new TElementWiseBinary<OpType>();
	}

	bool RegisterElementWiseBinaryOperators(NNX::FMLOperatorRegistryHlsl& Registry)
	{
#define OP(Name) Registry.OpAdd(TEXT(#Name), CreateElementWiseBinaryOperator<EMLElementWiseBinaryOperatorType::Name>, ValidateElementWiseBinaryOperator)
		OP(Add);
		//OP(And);
		OP(Div);
		//OP(Equal);
		//OP(Greater);
		//OP(GreaterOrEqual);
		//OP(Less);
		//OP(LessOrEqual);
		OP(Mod);
		OP(Mul);
		//OP(Or);
		OP(Prelu);
		OP(Pow);
		OP(Sub);
		//OP(Or);
#undef OP

		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
