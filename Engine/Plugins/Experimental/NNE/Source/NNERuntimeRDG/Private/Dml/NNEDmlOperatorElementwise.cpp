// Copyright Epic Games, Inc. All Rights Reserved.
#ifdef NNE_USE_DIRECTML
#include "NNEDmlOperator.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

/**
 * Utility function to get operator type
 */
template<typename TElementWiseOpDesc>
DML_OPERATOR_TYPE GetDmlElementWiseUnaryOpType();

#define OP_EW(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseUnaryOpType<DML_ELEMENT_WISE_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ELEMENT_WISE_##OpName; }

#define OP_AN(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseUnaryOpType<DML_ACTIVATION_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ACTIVATION_##OpName; }


OP_EW(IDENTITY)
OP_EW(ABS)
OP_EW(ACOS)
OP_EW(ACOSH)
OP_EW(ASIN)
OP_EW(ASINH)
OP_EW(ATAN)
OP_EW(ATANH)
// BitShift
// Cast
OP_EW(CEIL)
OP_EW(CLIP)
OP_EW(COS)
OP_EW(COSH)
OP_AN(ELU)
OP_EW(ERF)
OP_EW(EXP)
OP_EW(FLOOR)
OP_EW(IS_INFINITY)
OP_EW(IS_NAN)
OP_AN(HARDMAX)
OP_AN(HARD_SIGMOID)
OP_AN(LEAKY_RELU)
OP_EW(LOG)
OP_EW(NEGATE)
// Not
OP_EW(RECIP)
OP_AN(RELU)
OP_EW(ROUND)
OP_AN(SCALED_ELU)
OP_AN(SIGMOID)
OP_EW(SIGN)
OP_EW(SIN)
OP_EW(SINH)
OP_AN(SOFTPLUS)
OP_AN(SOFTSIGN)
OP_EW(SQRT)
OP_EW(TAN)
OP_EW(TANH)

#undef OP_EW
#undef OP_AN


/**
 * Utility function to get operator type
 */
//template<typename TActivationOpDesc>
//DML_OPERATOR_TYPE GetDmlActivationOpType();
//
//#define OP(OpName) \
//template<> \
//DML_OPERATOR_TYPE GetDmlActivationOpType<DML_ACTIVATION_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ACTIVATION_##OpName; }
//
//OP(ELU)
//OP(ERF)
//OP(HARDMAX)
//OP(HARD_SIGMOID)
//OP(LEAKY_RELU)
//OP(LINEAR)
//OP(LOG_SOFTMAX)
//OP(PARAMETERIZED_RELU)
//OP(PARAMETRIC_SOFTPLUS)
//OP(RELU)
//OP(SCALED_ELU)
//OP(SCALED_TANH)
//OP(SOFTMAX)
//OP(SOFTPLUS)
//OP(SOFTSIGN)
//OP(TANH)
//OP(THRESHOLDED_RELU)
//

/**
 * Utility function to get operator type
 */
template<typename TElementWiseOpDesc>
DML_OPERATOR_TYPE GetDmlElementWiseBinaryOpType();

#define OP_EW(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseBinaryOpType<DML_ELEMENT_WISE_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ELEMENT_WISE_##OpName; }

#define OP_AN(OpName) \
template<> \
DML_OPERATOR_TYPE GetDmlElementWiseBinaryOpType<DML_ACTIVATION_##OpName##_OPERATOR_DESC>() { return DML_OPERATOR_ACTIVATION_##OpName; }

OP_EW(ADD)
//OP_EW(LOGICAL_AND)
OP_EW(DIVIDE)
//OP_EW(LOGICAL_EQUALS)
//OP_EW(LOGICAL_GREATER_THAN)
//OP_EW(LOGICAL_LESS_THAN)
//OP_EW(MOD)
OP_EW(MULTIPLY)
//OP_EW(LOGICAL_OR)
OP_AN(PARAMETERIZED_RELU)
OP_EW(POW)
OP_EW(SUBTRACT)
//OP_EW(LOGICAL_XOR)

#undef OP_EW
#undef OP_AN


/**
 * Element-wise unary ML operator implementation
 */
template
<
	typename DmlElementWiseOpDescType, 
	NNECore::Internal::EElementWiseUnaryOperatorType OpType
>
class FOperatorDmlElementWiseUnary : public FOperatorDml
{
public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlElementWiseUnary();
	}

	virtual ~FOperatorDmlElementWiseUnary() = default;

private:

	FOperatorDmlElementWiseUnary() : Alpha(0.0f), Beta(0.0f), Gamma(0.0f), Min(TNumericLimits<float>::Min()), Max(TNumericLimits<float>::Max()), Num(1) {}
	float Alpha;
	float Beta;
	float Gamma;
	float Min;
	float Max;
	uint32 Num;

public:

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) override
	{
		Num = InputTensors[0].GetVolume();

		const NNECore::Internal::FTensor& InputTensorDesc = InputTensors[0];
		const NNECore::Internal::FTensor& OutputTensorDesc = OutputTensors[0];

		Alpha = Attributes.GetValueOrDefault(TEXT("alpha"), Alpha);
		Beta = Attributes.GetValueOrDefault(TEXT("beta"), Beta);
		Gamma = Attributes.GetValueOrDefault(TEXT("gamma"), Gamma);

		if constexpr (std::is_same_v<DmlElementWiseOpDescType, DML_ELEMENT_WISE_CLIP_OPERATOR_DESC>)
		{
			const FNNEAttributeValue* MinAttr = Attributes.GetAttributeValue(TEXT("min"));
			if(MinAttr)
			{
				if(MinAttr->GetType() != ENNEAttributeDataType::Float)
				{
					UE_LOG(LogNNE, Error, TEXT("Min attribute of clip must be float for DML inference"));
					return false;
				}
				
				Min = MinAttr->GetValue<float>();
			}
			const FNNEAttributeValue* MaxAttr = Attributes.GetAttributeValue(TEXT("max"));
			if(MaxAttr)
			{
				if(MaxAttr->GetType() != ENNEAttributeDataType::Float)
				{
					UE_LOG(LogNNE, Error, TEXT("Max attribute of clip must be float for DML inference"));
					return false;
				}
				Max = MaxAttr->GetValue<float>();
			}
		}

		// Initialize tensor descriptor (it's same for both input and output)
		DmlUtil::FTensorDesc	DmlTensorDesc{};

		if (!InitDmlTensorDesc(DmlTensorDesc, InputTensorDesc))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		DmlElementWiseOpDescType	DmlElemWiseOpDesc{};

		InitDmlOpDesc(DmlElemWiseOpDesc, DmlTensorDesc);

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = GetDmlElementWiseUnaryOpType<DmlElementWiseOpDescType>();
		DmlOpDesc.Desc = &DmlElemWiseOpDesc;

		return CreateOperator(Device, DmlOpDesc);
	}

private:

	template<typename OpDesc>
	void InitDmlOpDesc(OpDesc& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
	}

	void InitDmlOpDesc(DML_ELEMENT_WISE_CLIP_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Min = Min;
		Desc.Max = Max;
	}

	void InitDmlOpDesc(DML_ACTIVATION_SOFTPLUS_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Steepness = 1.0f;
	}

	void InitDmlOpDesc(DML_ACTIVATION_SCALED_ELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
		Desc.Gamma = Gamma;
	}

	void InitDmlOpDesc(DML_ACTIVATION_ELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
	}

	void InitDmlOpDesc(DML_ACTIVATION_HARD_SIGMOID_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
		Desc.Beta = Beta;
	}

	void InitDmlOpDesc(DML_ACTIVATION_LEAKY_RELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
		Desc.Alpha = Alpha;
	}
};

template<> FOperatorDmlElementWiseUnary<DML_ELEMENT_WISE_CLIP_OPERATOR_DESC, NNECore::Internal::EElementWiseUnaryOperatorType::Clip>::FOperatorDmlElementWiseUnary()
{
}

template<> FOperatorDmlElementWiseUnary<DML_ACTIVATION_SCALED_ELU_OPERATOR_DESC, NNECore::Internal::EElementWiseUnaryOperatorType::Selu>::FOperatorDmlElementWiseUnary()
	: Alpha(1.67326319217681884765625f), Beta(0.0f), Gamma(1.05070102214813232421875f), Num(1)
{
}

template<> FOperatorDmlElementWiseUnary<DML_ACTIVATION_ELU_OPERATOR_DESC, NNECore::Internal::EElementWiseUnaryOperatorType::Elu>::FOperatorDmlElementWiseUnary()
	: Alpha(1.0f), Beta(0.0f), Gamma(1.05070102214813232421875f), Num(1)
{
}

template<> FOperatorDmlElementWiseUnary<DML_ACTIVATION_HARD_SIGMOID_OPERATOR_DESC, NNECore::Internal::EElementWiseUnaryOperatorType::HardSigmoid>::FOperatorDmlElementWiseUnary()
	: Alpha(0.2f), Beta(0.5f), Gamma(0.0f), Num(1)
{
}

template<> FOperatorDmlElementWiseUnary<DML_ACTIVATION_LEAKY_RELU_OPERATOR_DESC, NNECore::Internal::EElementWiseUnaryOperatorType::LeakyRelu>::FOperatorDmlElementWiseUnary()
	: Alpha(0.01f), Beta(0.0f), Gamma(0.0f), Num(1)
{
}

/**
 * Element-wise binary ML operator implementation
 */
template
<
	typename TDmlElementWiseOpDescType,
	NNECore::Internal::EElementWiseBinaryOperatorType OpType
>
class FOperatorDmlElementWiseBinary : public FOperatorDml
{

public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlElementWiseBinary();
	}

private:

	FOperatorDmlElementWiseBinary() = default;

	uint32 Num { 1 };

public:

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) override
	{
		Num = OutputTensors[0].GetVolume();

		const NNECore::Internal::FTensor& InputATensorDesc = InputTensors[0];
		const NNECore::Internal::FTensor& InputBTensorDesc = InputTensors[1];
		const NNECore::Internal::FTensor& OutputTensorDesc = OutputTensors[0];

		// Initialize tensor descriptors
		DmlUtil::FTensorDesc	DmlInputATensorDesc{};
		DmlUtil::FTensorDesc	DmlInputBTensorDesc{};
		DmlUtil::FTensorDesc	DmlOutputTensorDesc{};

		if (!InitDmlTensorDesc(DmlInputATensorDesc, InputATensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlInputBTensorDesc, InputBTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlOutputTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		TDmlElementWiseOpDescType	DmlElemWiseOpDesc{};

		InitDmlOpDesc(DmlElemWiseOpDesc, DmlInputATensorDesc, DmlInputBTensorDesc, DmlOutputTensorDesc);

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = GetDmlElementWiseBinaryOpType<TDmlElementWiseOpDescType>();
		DmlOpDesc.Desc = &DmlElemWiseOpDesc;

		return CreateOperator(Device, DmlOpDesc);
	}

private:

	template<typename OpDesc>
	void InitDmlOpDesc(OpDesc& Desc, DmlUtil::FTensorDesc& LHSTensor, DmlUtil::FTensorDesc& RHSTensor, DmlUtil::FTensorDesc& OutputTensor)
	{
		Desc.ATensor = &LHSTensor.Desc;
		Desc.BTensor = &RHSTensor.Desc;
		Desc.OutputTensor = &OutputTensor.Desc;
	}

	void InitDmlOpDesc(DML_ELEMENT_WISE_POW_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& LHSTensor, DmlUtil::FTensorDesc& RHSTensor, DmlUtil::FTensorDesc& OutputTensor)
	{
		Desc.InputTensor = &LHSTensor.Desc;
		Desc.ExponentTensor = &RHSTensor.Desc;
		Desc.OutputTensor = &OutputTensor.Desc;
	}

	void InitDmlOpDesc(DML_ACTIVATION_PARAMETERIZED_RELU_OPERATOR_DESC& Desc, DmlUtil::FTensorDesc& LHSTensor, DmlUtil::FTensorDesc& RHSTensor, DmlUtil::FTensorDesc& OutputTensor)
	{
		Desc.InputTensor = &LHSTensor.Desc;
		Desc.SlopeTensor = &RHSTensor.Desc;
		Desc.OutputTensor = &OutputTensor.Desc;
	}
};

void RegisterElementWiseUnaryOperators()
{
#define OP(DmlOpDesc, OpName) FOperatorRegistryDml::Get()->OpAdd(TEXT(#OpName), FOperatorDmlElementWiseUnary<DmlOpDesc, NNECore::Internal::EElementWiseUnaryOperatorType::OpName>::Create)

	OP(DML_ELEMENT_WISE_ABS_OPERATOR_DESC, Abs);
	OP(DML_ELEMENT_WISE_ACOS_OPERATOR_DESC, Acos);
	OP(DML_ELEMENT_WISE_ACOSH_OPERATOR_DESC, Acosh);
	OP(DML_ELEMENT_WISE_ASIN_OPERATOR_DESC, Asin);
	OP(DML_ELEMENT_WISE_ASINH_OPERATOR_DESC, Asinh);
	OP(DML_ELEMENT_WISE_ATAN_OPERATOR_DESC, Atan);
	OP(DML_ELEMENT_WISE_ATANH_OPERATOR_DESC, Atanh);
	OP(DML_ELEMENT_WISE_CEIL_OPERATOR_DESC, Ceil);
	OP(DML_ELEMENT_WISE_CLIP_OPERATOR_DESC, Clip);
	OP(DML_ELEMENT_WISE_COS_OPERATOR_DESC, Cos);
	OP(DML_ELEMENT_WISE_COSH_OPERATOR_DESC, Cosh);
	OP(DML_ACTIVATION_ELU_OPERATOR_DESC, Elu);
	OP(DML_ELEMENT_WISE_ERF_OPERATOR_DESC, Erf);
	OP(DML_ELEMENT_WISE_EXP_OPERATOR_DESC, Exp);
	OP(DML_ELEMENT_WISE_FLOOR_OPERATOR_DESC, Floor);
	OP(DML_ELEMENT_WISE_IS_INFINITY_OPERATOR_DESC, IsInf);
	OP(DML_ELEMENT_WISE_IS_NAN_OPERATOR_DESC, IsNan);
	OP(DML_ACTIVATION_HARD_SIGMOID_OPERATOR_DESC, HardSigmoid);
	//OP(HardSwish);
	OP(DML_ACTIVATION_LEAKY_RELU_OPERATOR_DESC, LeakyRelu);
	OP(DML_ELEMENT_WISE_LOG_OPERATOR_DESC, Log);
	OP(DML_ELEMENT_WISE_NEGATE_OPERATOR_DESC, Neg);
	//OP(Not);
	OP(DML_ELEMENT_WISE_RECIP_OPERATOR_DESC, Reciprocal);
	OP(DML_ACTIVATION_RELU_OPERATOR_DESC, Relu);
	OP(DML_ELEMENT_WISE_ROUND_OPERATOR_DESC, Round);
	OP(DML_ACTIVATION_SCALED_ELU_OPERATOR_DESC, Selu);
	OP(DML_ACTIVATION_SIGMOID_OPERATOR_DESC, Sigmoid);
	OP(DML_ELEMENT_WISE_SIGN_OPERATOR_DESC, Sign);
	OP(DML_ELEMENT_WISE_SIN_OPERATOR_DESC, Sin);
	OP(DML_ELEMENT_WISE_SINH_OPERATOR_DESC, Sinh);
	OP(DML_ACTIVATION_SOFTPLUS_OPERATOR_DESC, Softplus);
	OP(DML_ACTIVATION_SOFTSIGN_OPERATOR_DESC, Softsign);
	OP(DML_ELEMENT_WISE_SQRT_OPERATOR_DESC, Sqrt);
	OP(DML_ELEMENT_WISE_TAN_OPERATOR_DESC, Tan);
	OP(DML_ELEMENT_WISE_TANH_OPERATOR_DESC, Tanh);

#undef OP
}

void RegisterElementWiseBinaryOperators()
{
#define OP(DmlOpDesc, OpName) FOperatorRegistryDml::Get()->OpAdd(TEXT(#OpName), FOperatorDmlElementWiseBinary<DmlOpDesc, NNECore::Internal::EElementWiseBinaryOperatorType::OpName>::Create)

	OP(DML_ELEMENT_WISE_ADD_OPERATOR_DESC, Add);
	// And
	OP(DML_ELEMENT_WISE_DIVIDE_OPERATOR_DESC, Div);
	// Equal
	// Greater
	// GreaterOrEqual
	// Less
	// LessOrEqual
	// Mod
	OP(DML_ELEMENT_WISE_MULTIPLY_OPERATOR_DESC, Mul);
	// Or
	OP(DML_ACTIVATION_PARAMETERIZED_RELU_OPERATOR_DESC, Prelu);
	OP(DML_ELEMENT_WISE_POW_OPERATOR_DESC, Pow);
	OP(DML_ELEMENT_WISE_SUBTRACT_OPERATOR_DESC, Sub);
	// Xor

#undef OP
}

struct FDmlOperatorElementWiseRegistrator
{
	FDmlOperatorElementWiseRegistrator()
	{
		RegisterElementWiseUnaryOperators();
		RegisterElementWiseBinaryOperators();
	}
};

static FDmlOperatorElementWiseRegistrator RegisterElementWiseOperators;

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML