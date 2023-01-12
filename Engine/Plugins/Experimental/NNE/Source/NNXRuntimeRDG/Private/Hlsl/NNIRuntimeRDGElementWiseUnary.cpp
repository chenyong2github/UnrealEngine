// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNIRuntimeRDGElementWiseUnary.h"
#include "NNEHlslShadersElementWiseUnaryCS.h"
#include "NNXRuntimeHLSLHelper.h"
#include "NNECoreAttributeMap.h"
#include "NNECoreTypes.h"
#include "NNECoreTensor.h"

namespace UE::NNIRuntimeRDG::Private::Hlsl
{
	DECLARE_GPU_STAT_NAMED(FNNEOperatorElementWiseUnary, TEXT("NNE.Operator.Hlsl.ElementWise.Unary"));

	using TElementWiseUnaryCS = typename UE::NNEHlslShaders::Internal::TElementWiseUnaryCS;
	using FElementWiseUnaryConstants = UE::NNEHlslShaders::Internal::FElementWiseUnaryConstants;

	/**
	 * Unary element-wise operator implementation
	 */
	template<EMLElementWiseUnaryOperatorType OpType>
	class TElementWiseUnary : public NNX::FMLOperatorHlsl
	{
	public:

		TElementWiseUnary() {}
		virtual ~TElementWiseUnary() = default;

	private:

		float Alpha = 0.0f;
		float Beta = 0.0f;
		float Gamma = 0.0f;

	public:

		virtual int PrepareOutputs(TConstArrayView<NNECore::Internal::FTensorRef> InputTensors, TArrayView<NNECore::Internal::FTensorRef> OutputTensors) const override
		{
			check(InputTensors.Num() == 1);
			check(OutputTensors.Num() == 1);
			OutputTensors[0]->SetShape(InputTensors[0]->GetShape());
			return 0;
		}

		virtual bool Initialize(TConstArrayView<NNECore::FTensorDesc> InputTensorDescs, TConstArrayView<NNECore::FTensorDesc> OutputTensorDescs, const NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensorDescs.Num() == 1);
			check(OutputTensorDescs.Num() == 1);

			Alpha = Attributes.GetValueOrDefault(TEXT("alpha"), Alpha);
			Beta = Attributes.GetValueOrDefault(TEXT("beta"), Beta);
			Gamma = Attributes.GetValueOrDefault(TEXT("gamma"), Gamma);
			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<NNX::FTensorRDGRef> InInputTensors, TConstArrayView<NNX::FTensorRDGRef> InOutputTensors) override
		{
			check(InInputTensors[0] != nullptr);
			check(InOutputTensors[0] != nullptr);
			FRDGBufferSRVRef InputSRV = GraphBuilder.CreateSRV(FRDGBufferSRVDesc(InInputTensors[0]->GetBuffer(), PF_R32_FLOAT));
			FRDGBufferUAVRef OutputUAV = GraphBuilder.CreateUAV(FRDGBufferUAVDesc(InOutputTensors[0]->GetBuffer(), PF_R32_FLOAT));
		
			int32 NumElements = InOutputTensors[0]->GetVolume();
			FIntVector ThreadGroupCount = NNX::ComputeElementWiseThreadGroups(NumElements, FElementWiseUnaryConstants::NUM_GROUP_THREADS);

			// Set parameters
			TElementWiseUnaryCS::FParameters* Params = GraphBuilder.AllocParameters<TElementWiseUnaryCS::FParameters>();
			Params->Input = InputSRV;
			Params->Output = OutputUAV;
			Params->Alpha = Alpha;
			Params->Beta = Beta;
			Params->Gamma = Gamma;
			Params->Num = NumElements;
			Params->ThreadCountX = ThreadGroupCount.X * FElementWiseUnaryConstants::NUM_GROUP_THREADS;

			TElementWiseUnaryCS::FPermutationDomain PermutationVector;

			PermutationVector.Set<TElementWiseUnaryCS::FOperatorType>(OpType);

			TShaderMapRef<TElementWiseUnaryCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel), PermutationVector);

			RDG_EVENT_SCOPE(GraphBuilder, "NNE.Operator.Hlsl.ElementWise.Unary");
			RDG_GPU_STAT_SCOPE(GraphBuilder, FNNEOperatorElementWiseUnary);
		
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("NNI.Operator.Hlsl.ElementWise.Unary.Dispatch"),
				ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
				ComputeShader,
				Params,
				ThreadGroupCount);
		}
	};

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::Selu>::TElementWiseUnary()
		: Alpha(1.67326319217681884765625f), Beta(0.0f), Gamma(1.05070102214813232421875f)
	{
	}

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::Elu>::TElementWiseUnary()
		: Alpha(1.0f), Beta(0.0f), Gamma(0.0f) 
	{
	}

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::HardSigmoid>::TElementWiseUnary()
		: Alpha(0.2f), Beta(0.5f), Gamma(0.0f)
	{
	}

	template<> TElementWiseUnary<EMLElementWiseUnaryOperatorType::LeakyRelu>::TElementWiseUnary()
		: Alpha(0.01f), Beta(0.0f), Gamma(0.0f)
	{
	}

	template<EMLElementWiseUnaryOperatorType OpType>
	NNX::FMLOperatorHlsl* CreateElementWiseUnaryOperator()
	{
		return new TElementWiseUnary<OpType>();
	}

	template<EMLElementWiseUnaryOperatorType OpType>
	bool ValidateElementWiseUnaryOperator(const NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNECore::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(ENNETensorDataType::Float);
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	template<>
	bool ValidateElementWiseUnaryOperator<EMLElementWiseUnaryOperatorType::Selu>(const NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNECore::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("alpha"), ENNEAttributeDataType::Float);
		AttributeValidator.AddOptional(TEXT("gamma"), ENNEAttributeDataType::Float);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(ENNETensorDataType::Float);
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	template<>
	bool ValidateElementWiseUnaryOperator<EMLElementWiseUnaryOperatorType::Elu>(const NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNECore::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("alpha"), ENNEAttributeDataType::Float);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(ENNETensorDataType::Float);
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	template<>
	bool ValidateElementWiseUnaryOperator<EMLElementWiseUnaryOperatorType::HardSigmoid>(const NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNECore::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("alpha"), ENNEAttributeDataType::Float);
		AttributeValidator.AddOptional(TEXT("beta"), ENNEAttributeDataType::Float);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(ENNETensorDataType::Float);
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	template<>
	bool ValidateElementWiseUnaryOperator<EMLElementWiseUnaryOperatorType::LeakyRelu>(const NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNECore::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		NNX::FAttributeValidator AttributeValidator;
		AttributeValidator.AddOptional(TEXT("alpha"), ENNEAttributeDataType::Float);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		NNX::FInputValidator InputValidator;
		InputValidator.AddSupportedType(ENNETensorDataType::Float);
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}
	
	bool RegisterElementWiseUnaryOperators(NNX::FMLOperatorRegistryHlsl& Registry)
	{
#define OP(Name) Registry.OpAdd(TEXT(#Name), CreateElementWiseUnaryOperator<EMLElementWiseUnaryOperatorType::Name>, ValidateElementWiseUnaryOperator<EMLElementWiseUnaryOperatorType::Name>)
		OP(Abs);
		OP(Acos);
		OP(Acosh);
		OP(Asin);
		OP(Asinh);
		OP(Atan);
		OP(Atanh);
		//OP(BitShift);
		//OP(Cast);
		OP(Ceil);
		//OP(Clip);
		OP(Cos);
		OP(Cosh);
		OP(Elu);
		OP(Erf);
		OP(Exp);
		OP(Floor);
		OP(IsInf);
		OP(IsNan);
		OP(HardSigmoid);
		OP(HardSwish);
		OP(LeakyRelu);
		OP(Log);
		OP(Neg);
		//OP(Not);
		OP(Reciprocal);
		OP(Relu);
		OP(Round);
		OP(Selu);
		OP(Sigmoid);
		OP(Sign);
		OP(Sin);
		OP(Sinh);
		OP(Softplus);
		OP(Softsign);
		OP(Sqrt);
		OP(Tan);
		OP(Tanh);
#undef OP

		return true;
	}
} // UE::NNIRuntimeRDG::Private::Hlsl
