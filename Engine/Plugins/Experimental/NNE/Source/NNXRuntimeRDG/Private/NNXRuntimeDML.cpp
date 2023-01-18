// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeRDG.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"
#include "NNEUtilsModelOptimizer.h"
#include "NNXOperator.h"

#include "NNECoreAttributeMap.h"

#include "HAL/FileManager.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "Algo/Find.h"

#ifdef NNE_USE_DIRECTML

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <unknwn.h>
#include "Microsoft/COMPointer.h"
#include "DirectML.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

// DirectML is implemented using COM on all platforms
#ifdef IID_GRAPHICS_PPV_ARGS
#define DML_PPV_ARGS(x) __uuidof(*x), IID_PPV_ARGS_Helper(x)
#else
#define DML_PPV_ARGS(x) IID_PPV_ARGS(x)
#endif

#include "ID3D12DynamicRHI.h"

#define NNE_USE_D3D12_RESOURCES
#define NNX_RUNTIME_DML_NAME TEXT("NNXRuntimeDml")


namespace NNX
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

namespace DmlUtil
{
	struct FTensorDesc
	{
		DML_BUFFER_TENSOR_DESC										BuffDesc;
		DML_TENSOR_DESC												Desc;
		TArray<uint32, TInlineAllocator<FTensorShape::MaxRank>>		Sizes;
		TArray<uint32, TInlineAllocator<FTensorShape::MaxRank>>		Strides;
	};

	void SetTensorStrides(FTensorDesc& TensorDesc, const UE::NNECore::Internal::FTensor& InputDesc)
	{
		uint32 CurrStride = 1;

		TensorDesc.Strides.SetNum(InputDesc.GetShape().Rank());
		
		for (int32 i = InputDesc.GetShape().Rank() - 1; i >= 0; --i)
		{
			TensorDesc.Strides[i] = CurrStride;
			CurrStride *= InputDesc.GetShape().GetData()[i];
		}
	}

	void SetTensorSizesAndStridesForBroadcast(FTensorDesc& TensorDesc, const UE::NNECore::Internal::FTensor& InputDesc, const UE::NNECore::Internal::FTensor& TargetDesc)
	{
		static_assert(FTensorShape::MaxRank <= 8);
		
		const uint32 TargetDimension = TargetDesc.GetShape().Rank() != -1 ? TargetDesc.GetShape().Rank() : InputDesc.GetShape().Rank();
		checkf(TargetDesc.GetShape().Rank() >= InputDesc.GetShape().Rank(), TEXT("Can't broadcast tensor from rank %d to rank %d, should be inferior or equal."), InputDesc.GetShape().Rank(), TargetDimension);
		
		TensorDesc.Sizes.SetNum(TargetDimension);
		TensorDesc.Strides.SetNum(TargetDimension);

		const int32 DimensionOffset = int32(TargetDimension - InputDesc.GetShape().Rank());
		
		for (int32 i = 0; i < (int32) TargetDimension; ++i)
		{
			TensorDesc.Sizes[i] = i < DimensionOffset ? 1 : InputDesc.GetShape().GetData()[i - DimensionOffset];
		}

		uint32 CurrStride = 1;

		for (int32 i = TargetDimension - 1; i >= 0; --i)
		{
			const bool bBroadcast = TensorDesc.Sizes[i] < TargetDesc.GetShape().GetData()[i];

			TensorDesc.Strides[i] = bBroadcast ? 0 : CurrStride;
			CurrStride *= TensorDesc.Sizes[i];

			TensorDesc.Sizes[i] = TargetDesc.GetShape().GetData()[i];
		}
	}

	inline bool IsSameShape(const UE::NNECore::Internal::FTensor& Left, const UE::NNECore::Internal::FTensor& Right)
	{
		if (Left.GetShape().Rank() != Right.GetShape().Rank())
		{
			return false;
		}
		
		for (int32 Idx = 0; Idx < Left.GetShape().Rank(); ++Idx)
		{
			if (Left.GetShape().GetData()[Idx] != Right.GetShape().GetData()[Idx])
			{
				return false;
			}
		}

		return true;
	}

	DML_TENSOR_DATA_TYPE GetTensorDataType(ENNETensorDataType DataType)
	{
		switch (DataType)
		{
		case ENNETensorDataType::Double:
			return DML_TENSOR_DATA_TYPE_FLOAT64;

		case ENNETensorDataType::Float:
			return DML_TENSOR_DATA_TYPE_FLOAT32;

		case ENNETensorDataType::Half:
			return DML_TENSOR_DATA_TYPE_FLOAT16;

		case ENNETensorDataType::UInt64:
			return DML_TENSOR_DATA_TYPE_UINT64;

		case ENNETensorDataType::UInt32:
			return DML_TENSOR_DATA_TYPE_UINT32;

		case ENNETensorDataType::UInt16:
			return DML_TENSOR_DATA_TYPE_UINT16;

		case ENNETensorDataType::UInt8:
			return DML_TENSOR_DATA_TYPE_UINT8;

		case ENNETensorDataType::Int64:
			return DML_TENSOR_DATA_TYPE_INT64;

		case ENNETensorDataType::Int32:
			return DML_TENSOR_DATA_TYPE_INT32;

		case ENNETensorDataType::Int16:
			return DML_TENSOR_DATA_TYPE_INT16;

		case ENNETensorDataType::Int8:
			return DML_TENSOR_DATA_TYPE_INT8;

		default:
			return DML_TENSOR_DATA_TYPE_UNKNOWN;
		}
	}

	inline uint64 CalculateBufferSize(const FTensorDesc& DmlTensor, const UE::NNECore::Internal::FTensor& Desc)
	{
		uint64 ElemSizeInBytes = Desc.GetElemByteSize();
		
		if (ElemSizeInBytes == 0)
		{
			return 0;
		}

		uint64 MinSizeInBytes = 0;		
		uint32 IndexOfLastElement = 0;

		for (int32 i = 0; i < DmlTensor.Sizes.Num(); ++i)
		{
			IndexOfLastElement += (DmlTensor.Sizes[i] - 1) * DmlTensor.Strides[i];
		}

		MinSizeInBytes = (static_cast<uint64>(IndexOfLastElement) + 1) * ElemSizeInBytes;
		
		// Round up to the nearest 4 bytes.
		MinSizeInBytes = (MinSizeInBytes + 3) & ~3ull;

		return MinSizeInBytes;
	}
}

//
//
//
class FDeviceContextDml
{
public:

	uint32							DeviceIndex;
	ID3D12Device*					D3D12Device{ nullptr }; // Borrowed reference from RHI
	TComPtr<IDMLDevice>				Device{ nullptr };
	TComPtr<IDMLCommandRecorder>	CmdRec{ nullptr };
};

//
// DirectML operator
//
class FMLOperatorDml
{
public:

	virtual ~FMLOperatorDml() = default;

	virtual bool Initialize(FDeviceContextDml* DevCtx, TArrayView<const UE::NNECore::Internal::FTensor> InputTensors, TArrayView<const UE::NNECore::Internal::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) = 0;

	IDMLOperator* GetOperator()
	{
		return DmlOp;
	}

protected:

	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const UE::NNECore::Internal::FTensor& TensorDesc)
	{
		DML_TENSOR_DATA_TYPE DmlDataType = DmlUtil::GetTensorDataType(TensorDesc.GetDataType());

		if (DmlDataType == DML_TENSOR_DATA_TYPE_UNKNOWN)
		{
			DmlTensorDesc.BuffDesc = DML_BUFFER_TENSOR_DESC{};
			DmlTensorDesc.Desc = DML_TENSOR_DESC{};

			return false;
		}

		DmlTensorDesc.Sizes = TensorDesc.GetShape().GetData();
		// TODO: Support tensor padding using strides defined in FMLTensorDesc
		//DmlUtil::SetTensorStrides(DmlTensorDesc, TensorDesc.Strides);
		
		DML_BUFFER_TENSOR_DESC& BuffDesc = DmlTensorDesc.BuffDesc;

		BuffDesc = DML_BUFFER_TENSOR_DESC{};
		BuffDesc.DataType = DmlDataType;
		BuffDesc.Flags = TensorDesc.HasPreparedData() ? DML_TENSOR_FLAG_OWNED_BY_DML : DML_TENSOR_FLAG_NONE;
		BuffDesc.DimensionCount = TensorDesc.GetShape().Rank();
		BuffDesc.Sizes = DmlTensorDesc.Sizes.GetData();
		BuffDesc.Strides = nullptr;
		BuffDesc.TotalTensorSizeInBytes = TensorDesc.GetDataSize();

		DmlTensorDesc.Desc = DML_TENSOR_DESC{ DML_TENSOR_TYPE_BUFFER, &DmlTensorDesc.BuffDesc };

		return true;
	}

	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const UE::NNECore::Internal::FTensor& TensorDesc, const UE::NNECore::Internal::FTensor& BroadcastDesc)
	{
		DML_TENSOR_DATA_TYPE DmlDataType = DmlUtil::GetTensorDataType(TensorDesc.GetDataType());

		if (DmlDataType == DML_TENSOR_DATA_TYPE_UNKNOWN)
		{
			DmlTensorDesc.BuffDesc = DML_BUFFER_TENSOR_DESC{};
			DmlTensorDesc.Desc = DML_TENSOR_DESC{};

			return false;
		}

		if (DmlUtil::IsSameShape(TensorDesc, BroadcastDesc))
		{
			DmlTensorDesc.Sizes = TensorDesc.GetShape().GetData();
			DmlUtil::SetTensorStrides(DmlTensorDesc, TensorDesc);
		}
		else if (TensorDesc.GetShape().Rank() > BroadcastDesc.GetShape().Rank())
		{
			return false;
		}
		else
		{
			DmlUtil::SetTensorSizesAndStridesForBroadcast(DmlTensorDesc, TensorDesc, BroadcastDesc);
		}

		//UE_LOG(LogNNX, Warning, TEXT("DmlTensorDesc:%d,%d,%d -> %d,%d,%d"),
		//	TensorDesc.Sizes[0],
		//	TensorDesc.Dimension > 1 ? TensorDesc.Sizes[1] : 0,
		//	TensorDesc.Dimension > 2 ? TensorDesc.Sizes[2] : 0,
		//	DmlTensorDesc.Sizes[0],
		//	DmlTensorDesc.Dimension > 1 ? DmlTensorDesc.Sizes[1] : 0,
		//	DmlTensorDesc.Dimension > 2 ? DmlTensorDesc.Sizes[2] : 0
		//);

		//UE_LOG(LogNNX, Warning, TEXT("DmlTensorStrides:%d,%d,%d"),
		//	DmlTensorDesc.Strides[0], DmlTensorDesc.Strides[1], DmlTensorDesc.Strides[2]
		//);

		check(DmlTensorDesc.Strides.Num() == DmlTensorDesc.Sizes.Num());
		
		DML_BUFFER_TENSOR_DESC& BuffDesc = DmlTensorDesc.BuffDesc;
		
		BuffDesc = DML_BUFFER_TENSOR_DESC{};

		BuffDesc.DataType = DmlDataType;
		BuffDesc.Flags = TensorDesc.HasPreparedData() ? DML_TENSOR_FLAG_OWNED_BY_DML : DML_TENSOR_FLAG_NONE;
		BuffDesc.DimensionCount = DmlTensorDesc.Sizes.Num();
		BuffDesc.Sizes = DmlTensorDesc.Sizes.GetData();
		BuffDesc.Strides = DmlTensorDesc.Strides.GetData();
		BuffDesc.TotalTensorSizeInBytes = DmlUtil::CalculateBufferSize(DmlTensorDesc, TensorDesc);
		
		DmlTensorDesc.Desc = DML_TENSOR_DESC{ DML_TENSOR_TYPE_BUFFER, &DmlTensorDesc.BuffDesc };

		return true;
	}
	
	bool CreateOperator(const DML_OPERATOR_DESC& DmlOpDesc)
	{
		IDMLDevice* Device = DevCtx->Device;

		// Create operator
		IDMLOperator* Op = nullptr;

		HRESULT Res;

		Res = Device->CreateOperator(&DmlOpDesc, DML_PPV_ARGS(&Op));
		if (!Op)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:Failed to create DML operator, hres:%d"), Res);
			return false;
		}

		DmlOp = Op;

		return DmlOp.IsValid();
	}

	FDeviceContextDml*			DevCtx;
	TComPtr<IDMLOperator>		DmlOp;
};

/**
 * DirectML ML operator registry
 */
using FMLOperatorRegistryDml = TOperatorRegistryRDG<FMLOperatorDml>;

/**
 * Element-wise unary ML operator implementation
 */
template
<
	typename DmlElementWiseOpDescType, 
	EMLElementWiseUnaryOperatorType OpType
>
class FMLOperatorDmlElementWiseUnary : public FMLOperatorDml
{
public:

	static FMLOperatorDml* Create()
	{
		return new FMLOperatorDmlElementWiseUnary();
	}

	virtual ~FMLOperatorDmlElementWiseUnary() = default;

private:

	FMLOperatorDmlElementWiseUnary() : Alpha(0.0f), Beta(0.0f), Gamma(0.0f), Num(1) {}
	float Alpha;
	float Beta;
	float Gamma;
	uint32 Num;

public:

	//
	//
	//
	virtual bool Initialize(FDeviceContextDml* InDevCtx, TArrayView<const UE::NNECore::Internal::FTensor> InputTensors, TArrayView<const UE::NNECore::Internal::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
	{
		Num = InputTensors[0].GetVolume();

		DevCtx = InDevCtx;

		const UE::NNECore::Internal::FTensor& InputTensorDesc = InputTensors[0];
		const UE::NNECore::Internal::FTensor& OutputTensorDesc = OutputTensors[0];

		Alpha = Attributes.GetValueOrDefault(TEXT("alpha"), Alpha);
		Beta = Attributes.GetValueOrDefault(TEXT("beta"), Beta);
		Gamma = Attributes.GetValueOrDefault(TEXT("gamma"), Gamma);

		// Initialize tensor descriptor (it's same for both input and output)
		DmlUtil::FTensorDesc	DmlTensorDesc{};

		if (!InitDmlTensorDesc(DmlTensorDesc, InputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		DmlElementWiseOpDescType	DmlElemWiseOpDesc{};

		InitDmlOpDesc(DmlElemWiseOpDesc, DmlTensorDesc);

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = GetDmlElementWiseUnaryOpType<DmlElementWiseOpDescType>();
		DmlOpDesc.Desc = &DmlElemWiseOpDesc;

		return CreateOperator(DmlOpDesc);
	}

private:

	template<typename OpDesc>
	void InitDmlOpDesc(OpDesc& Desc, DmlUtil::FTensorDesc& TensorDesc)
	{
		Desc.InputTensor = &TensorDesc.Desc;
		Desc.OutputTensor = &TensorDesc.Desc;
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

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_SCALED_ELU_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::Selu>::FMLOperatorDmlElementWiseUnary()
	: Alpha(1.67326319217681884765625f), Beta(0.0f), Gamma(1.05070102214813232421875f), Num(1)
{
}

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_ELU_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::Elu>::FMLOperatorDmlElementWiseUnary()
	: Alpha(1.0f), Beta(0.0f), Gamma(1.05070102214813232421875f), Num(1)
{
}

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_HARD_SIGMOID_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::HardSigmoid>::FMLOperatorDmlElementWiseUnary()
	: Alpha(0.2f), Beta(0.5f), Gamma(0.0f), Num(1)
{
}

template<> FMLOperatorDmlElementWiseUnary<DML_ACTIVATION_LEAKY_RELU_OPERATOR_DESC, EMLElementWiseUnaryOperatorType::LeakyRelu>::FMLOperatorDmlElementWiseUnary()
	: Alpha(0.01f), Beta(0.0f), Gamma(0.0f), Num(1)
{
}

/**
 * Element-wise binary ML operator implementation
 */
template
<
	typename TDmlElementWiseOpDescType,
	EMLElementWiseBinaryOperatorType OpType
>
class FMLOperatorDmlElementWiseBinary : public FMLOperatorDml
{

public:

	static FMLOperatorDml* Create()
	{
		return new FMLOperatorDmlElementWiseBinary();
	}

private:

	FMLOperatorDmlElementWiseBinary() = default;

	uint32 Num { 1 };

public:

	//
	//
	//
	virtual bool Initialize(FDeviceContextDml* InDevCtx, TArrayView<const UE::NNECore::Internal::FTensor> InputTensors, TArrayView<const UE::NNECore::Internal::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
	{
		// TODO: Setup attributes
		Num = OutputTensors[0].GetVolume();

		DevCtx = InDevCtx;

		const UE::NNECore::Internal::FTensor& InputATensorDesc = InputTensors[0];
		const UE::NNECore::Internal::FTensor& InputBTensorDesc = InputTensors[1];
		const UE::NNECore::Internal::FTensor& OutputTensorDesc = OutputTensors[0];

		// Initialize tensor descriptors
		DmlUtil::FTensorDesc	DmlInputATensorDesc{};
		DmlUtil::FTensorDesc	DmlInputBTensorDesc{};
		DmlUtil::FTensorDesc	DmlOutputTensorDesc{};

		if (!InitDmlTensorDesc(DmlInputATensorDesc, InputATensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlInputBTensorDesc, InputBTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlOutputTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		TDmlElementWiseOpDescType	DmlElemWiseOpDesc{};

		InitDmlOpDesc(DmlElemWiseOpDesc, DmlInputATensorDesc, DmlInputBTensorDesc, DmlOutputTensorDesc);

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = GetDmlElementWiseBinaryOpType<TDmlElementWiseOpDescType>();
		DmlOpDesc.Desc = &DmlElemWiseOpDesc;

		return CreateOperator(DmlOpDesc);
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

/**
 * Gemm
 */
class FMLOperatorDmlGemm : public FMLOperatorDml
{
public:

	static FMLOperatorDml* Create()
	{
		return new FMLOperatorDmlGemm();
	}

	//
	//
	//
	virtual bool Initialize(FDeviceContextDml* InDevCtx, TArrayView<const UE::NNECore::Internal::FTensor> InputTensors, TArrayView<const UE::NNECore::Internal::FTensor> OutputTensors, const UE::NNECore::FAttributeMap& Attributes) override
	{
		// Setup attributes
		float Alpha = 1.0f;
		float Beta = 1.0f;
		int32 TransA = 0;
		int32 TransB = 0;
		
		Alpha = Attributes.GetValueOrDefault(TEXT("alpha"), Alpha);
		Beta = Attributes.GetValueOrDefault(TEXT("beta"), Beta);
		TransA = Attributes.GetValueOrDefault(TEXT("transA"), TransA);
		TransB = Attributes.GetValueOrDefault(TEXT("transB"), TransB);
		
		DevCtx = InDevCtx;

		const UE::NNECore::Internal::FTensor& InputATensorDesc = InputTensors[0];
		const UE::NNECore::Internal::FTensor& InputBTensorDesc = InputTensors[1];
		const UE::NNECore::Internal::FTensor& OutputTensorDesc = OutputTensors[0];

		// Initialize tensor descriptors
		DmlUtil::FTensorDesc	DmlInputATensorDesc{};
		DmlUtil::FTensorDesc	DmlInputBTensorDesc{};
		DmlUtil::FTensorDesc	DmlInputCTensorDesc{};
		DmlUtil::FTensorDesc	DmlOutputTensorDesc{};

		if (!InitDmlTensorDesc(DmlInputATensorDesc, InputATensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!InitDmlTensorDesc(DmlInputBTensorDesc, InputBTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (InputTensors.Num() > 2)
		{
			const UE::NNECore::Internal::FTensor& InputCTensorDesc = InputTensors[2];

			if (!InitDmlTensorDesc(DmlInputCTensorDesc, InputCTensorDesc, OutputTensorDesc))
			{
				UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
				return false;
			}
		}

		if (!InitDmlTensorDesc(DmlOutputTensorDesc, OutputTensorDesc))
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		DML_GEMM_OPERATOR_DESC	DmlGemmOpDesc{};

		DmlGemmOpDesc.ATensor = &DmlInputATensorDesc.Desc;
		DmlGemmOpDesc.BTensor = &DmlInputBTensorDesc.Desc;
		DmlGemmOpDesc.CTensor = InputTensors.Num() > 2 ? &DmlInputCTensorDesc.Desc : nullptr;
		DmlGemmOpDesc.OutputTensor = &DmlOutputTensorDesc.Desc;
		DmlGemmOpDesc.Alpha = Alpha;
		DmlGemmOpDesc.Beta = Beta;
		DmlGemmOpDesc.TransA = TransA ? DML_MATRIX_TRANSFORM_TRANSPOSE : DML_MATRIX_TRANSFORM_NONE;
		DmlGemmOpDesc.TransB = TransB ? DML_MATRIX_TRANSFORM_TRANSPOSE : DML_MATRIX_TRANSFORM_NONE;

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = DML_OPERATOR_GEMM;
		DmlOpDesc.Desc = &DmlGemmOpDesc;

		return CreateOperator(DmlOpDesc);
	}

};

//
//
//
class FMLInferenceModelDml : public FMLInferenceModelRDG
{
	class FGraphBuilder;
	class FBindingTable;
	class FDebugName;

public:

	FMLInferenceModelDml();
	~FMLInferenceModelDml();

	bool Init(TConstArrayView<uint8> ModelData, FDeviceContextDml* InDevCtx);

protected:

	virtual void AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder) override;
	virtual int PrepareTensorShapesAndData() override;

private:

	bool InitCompiledOp(TConstArrayView<int32> OpInputIndices, uint64 TensorDataSize);

	FMLOperatorDml* OpCreate(const FString& Name, TArrayView<const UE::NNECore::Internal::FTensor> InputTensorDesc, TArrayView<const UE::NNECore::Internal::FTensor> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes);
		
	FBufferRHIRef CreateRHIBuffer(FRHICommandListImmediate& RHICmdList, uint32 Size, EBufferUsageFlags Usage, ERHIAccess Access, const TCHAR* DbgName);
	ID3D12Resource* CreateD3D12Buffer(uint32 Size, D3D12_RESOURCE_STATES ResourceState = D3D12_RESOURCE_STATE_COMMON, D3D12_HEAP_TYPE HeapType = D3D12_HEAP_TYPE_DEFAULT, const TCHAR* DebugName = nullptr);
	
	// TODO: FIXME: This should go into RDG
	static constexpr int32 MaxNumInputs = 32;
	static constexpr int32 MaxNumOutputs = 4;

	using FRHIBufferInputArray = TArray<FRHIBuffer*, TInlineAllocator<MaxNumInputs>>;
	using FRHIBufferOutputArray = TArray<FRHIBuffer*, TInlineAllocator<MaxNumOutputs>>;

	TComPtr<IDMLOperatorInitializer>	OpInit;
	TComPtr<IDMLCompiledOperator>		CompiledOp;
	FDeviceContextDml*					DevCtx;
	TUniquePtr<FBindingTable>			BindingTable;
	TComPtr<ID3D12DescriptorHeap>		DescHeap;
	uint32								DescCount;
	uint32								DescSize;

	FRHIBufferInputArray				InputBuffers;
	FRHIBufferOutputArray				OutputBuffers;
#ifdef NNE_USE_D3D12_RESOURCES
	TComPtr<ID3D12Resource>				PersistBuff;
	TComPtr<ID3D12Resource>				TempBuff;
#else
	FBufferRHIRef						PersistBuff;
#endif
	uint64								MemSizeTemp;
	uint64								MemSizePersist;
	ID3D12DynamicRHI*					DynamicRHI;
};

//
//
//
class FMLRuntimeDml : public FMLRuntimeRDG
{
public:

	FMLRuntimeDml() = default;
	virtual ~FMLRuntimeDml();

	virtual FString GetRuntimeName() const override
	{
		return NNX_RUNTIME_DML_NAME;
	}
	
	virtual EMLRuntimeSupportFlags GetSupportFlags() const override
	{
		return EMLRuntimeSupportFlags::RDG;
	}

	virtual TArray<uint8> CreateModelData(FString FileType, TConstArrayView<uint8> FileData) override
	{
		if (!CanCreateModelData(FileType, FileData))
		{
			return {};
		}

		TUniquePtr<IModelOptimizer> Optimizer = UE::NNEUtils::Internal::CreateONNXToNNEModelOptimizer();

		FNNIModelRaw InputModel;
		InputModel.Data = FileData;
		InputModel.Format = ENNXInferenceFormat::ONNX;

		FNNIModelRaw OutputModel;
		if (!Optimizer->Optimize(InputModel, OutputModel, {}))
		{
			return {};
		}

		return ConvertToModelData(OutputModel.Data);
	};

	virtual TUniquePtr<FMLInferenceModel> CreateModel(TConstArrayView<uint8> ModelData) override
	{
		if (!CanCreateModel(ModelData))
		{
			return TUniquePtr<FMLInferenceModel>();
		}

		// Create the model and initialize it with the data not including the header
		FMLInferenceModelDml* Model = new FMLInferenceModelDml();
		if (!Model->Init(ModelData, &Ctx))
		{
			delete Model;
			return TUniquePtr<FMLInferenceModel>();
		}
		return TUniquePtr<FMLInferenceModel>(Model);
	}

	bool Init(bool bRegisterOnlyOperators);

private:

	bool RegisterElementWiseUnaryOperators();
	bool RegisterElementWiseBinaryOperators();
	bool RegisterGemmOperator();
	
	FDeviceContextDml		Ctx;
};

//
//
//
static TUniquePtr<FMLRuntimeDml> GDmlRuntime;

//
//
//
FMLRuntimeDml::~FMLRuntimeDml()
{
}

//
//
//
bool FMLRuntimeDml::Init(bool bRegisterOnlyOperators)
{
	RegisterElementWiseUnaryOperators();
	RegisterElementWiseBinaryOperators();
	RegisterGemmOperator();

	if (bRegisterOnlyOperators)
	{
		UE_LOG(LogNNX, Display, TEXT("Registering only operators"));
		return true;
	}

	HRESULT Res;

	// In order to use DirectML we need D3D12
	ID3D12DynamicRHI* RHI = nullptr;

	if (GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12)
	{
		RHI = GetID3D12PlatformDynamicRHI();

		if (!RHI)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:%s RHI is not supported by DirectML"), GDynamicRHI->GetName());
			return false;
		}
	}
	else
	{
		if (GDynamicRHI)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:%s RHI is not supported by DirectML"), GDynamicRHI->GetName());
			return false;
		}
		else
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:No RHI found"));
			return false;
		}
	}

	Ctx.DeviceIndex = 0;
	Ctx.D3D12Device = RHI->RHIGetDevice(Ctx.DeviceIndex);

#if PLATFORM_WINDOWS
	ID3D12Device5* D3D12Device5 = nullptr;
	
	Res = Ctx.D3D12Device->QueryInterface(&D3D12Device5);
	if (D3D12Device5)
	{
		uint32	NumCommands = 0;

		Res = D3D12Device5->EnumerateMetaCommands(&NumCommands, nullptr);
		if (NumCommands)
		{
			UE_LOG(LogNNX, Verbose, TEXT("D3D12 Meta commands:%u"), NumCommands);

			TArray<D3D12_META_COMMAND_DESC>	MetaCmds;

			MetaCmds.SetNumUninitialized(NumCommands);

			Res = D3D12Device5->EnumerateMetaCommands(&NumCommands, MetaCmds.GetData());
			for (uint32 Idx = 0; Idx < NumCommands; ++Idx)
			{
				const D3D12_META_COMMAND_DESC& Desc = MetaCmds[Idx];

				UE_LOG(LogNNX, Verbose, TEXT("   %s"), Desc.Name);
			}
		}
	}
#endif

	DML_CREATE_DEVICE_FLAGS DmlCreateFlags = DML_CREATE_DEVICE_FLAG_NONE;

	// Set debugging flags
	if (RHI->IsD3DDebugEnabled())
	{
		DmlCreateFlags |= DML_CREATE_DEVICE_FLAG_DEBUG;
	}

	Res = DMLCreateDevice(Ctx.D3D12Device, DmlCreateFlags, DML_PPV_ARGS(&Ctx.Device));
	if (!Ctx.Device)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create DirectML device, res:%x"), Res);
		return false;
	}

	DML_FEATURE_QUERY_TENSOR_DATA_TYPE_SUPPORT	Fp16Query = { DML_TENSOR_DATA_TYPE_FLOAT16 };
	DML_FEATURE_DATA_TENSOR_DATA_TYPE_SUPPORT	Fp16Supported = {};
	
	Ctx.Device->CheckFeatureSupport(DML_FEATURE_TENSOR_DATA_TYPE_SUPPORT, sizeof(Fp16Query), &Fp16Query, sizeof(Fp16Supported), &Fp16Supported);

	DML_FEATURE_LEVEL					FeatureLevels[] = { DML_FEATURE_LEVEL_5_0 };
	DML_FEATURE_QUERY_FEATURE_LEVELS	FeatureLevelQuery = { UE_ARRAY_COUNT(FeatureLevels), FeatureLevels };
	DML_FEATURE_DATA_FEATURE_LEVELS		FeatureLevelSupported = {};

	Res = Ctx.Device->CheckFeatureSupport(DML_FEATURE_FEATURE_LEVELS, sizeof(FeatureLevelQuery), &FeatureLevelQuery, sizeof(FeatureLevelSupported), &FeatureLevelSupported);
	if (FAILED(Res) || FeatureLevelSupported.MaxSupportedFeatureLevel < DML_FEATURE_LEVEL_5_0)
	{
		UE_LOG(LogNNX, Warning, TEXT("DirectML feature level %x not supported"), FeatureLevels[0]);
		return false;
	}

	Res = Ctx.Device->CreateCommandRecorder(DML_PPV_ARGS(&Ctx.CmdRec));
	if (!Ctx.CmdRec)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create DML command recorder, res:%x"), Res);
		return false;
	}

	return true;
}

//
//
//
bool FMLRuntimeDml::RegisterElementWiseUnaryOperators()
{
#define OP(DmlOpDesc, OpName) FMLOperatorRegistryDml::Get()->OpAdd(TEXT(#OpName), FMLOperatorDmlElementWiseUnary<DmlOpDesc, EMLElementWiseUnaryOperatorType::OpName>::Create)

	OP(DML_ELEMENT_WISE_ABS_OPERATOR_DESC, Abs);
	OP(DML_ELEMENT_WISE_ACOS_OPERATOR_DESC, Acos);
	OP(DML_ELEMENT_WISE_ACOSH_OPERATOR_DESC, Acosh);
	OP(DML_ELEMENT_WISE_ASIN_OPERATOR_DESC, Asin);
	OP(DML_ELEMENT_WISE_ASINH_OPERATOR_DESC, Asinh);
	OP(DML_ELEMENT_WISE_ATAN_OPERATOR_DESC, Atan);
	OP(DML_ELEMENT_WISE_ATANH_OPERATOR_DESC, Atanh);
	OP(DML_ELEMENT_WISE_CEIL_OPERATOR_DESC, Ceil);
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

	return true;
}

//
//
//
bool FMLRuntimeDml::RegisterElementWiseBinaryOperators()
{
#define OP(DmlOpDesc, OpName) FMLOperatorRegistryDml::Get()->OpAdd(TEXT(#OpName), FMLOperatorDmlElementWiseBinary<DmlOpDesc, EMLElementWiseBinaryOperatorType::OpName>::Create)

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

	return true;
}

//
//
//
bool FMLRuntimeDml::RegisterGemmOperator()
{
	FMLOperatorRegistryDml::Get()->OpAdd(TEXT("Gemm"), FMLOperatorDmlGemm::Create);
	return true;
}

//
//
//
class FMLInferenceModelDml::FDebugName
{
	static constexpr int32 Size = 128;

public:

	FDebugName()
	{
		Str[0] = '\0';
		Length = 0;
	}

	FDebugName(const FString& InStr)
	{
		FTCHARToUTF8 Conv(*InStr);

		Length = Conv.Length() + 1 < Size ? Conv.Length() + 1 : Size - 1;
		FCStringAnsi::Strncpy(Str, Conv.Get(), Length);
		Str[Length] = '\0';
	}

	FDebugName(FStringView InStr)
	{
		FTCHARToUTF8 Conv(InStr.GetData());

		Length = Conv.Length() + 1 < Size ? Conv.Length() + 1 : Size - 1;
		FCStringAnsi::Strncpy(Str, Conv.Get(), Length);
		Str[Length] = '\0';
	}
	~FDebugName()
	{
	}

	const char* Get() const
	{
		return Str;
	}

private:

	char	Str[Size];
	int32	Length;
};

//
//
//
class FMLInferenceModelDml::FBindingTable
{
public:

	bool Init(FMLInferenceModelDml* InModel)
	{
		Model = InModel;
		DynamicRHI = InModel->DynamicRHI;

		return true;
	}

#ifdef NNE_USE_D3D12_RESOURCES
	void Bind(IDMLOperatorInitializer* OpInit, TConstArrayView<FRHIBuffer*> InputBuffers, ID3D12Resource* PersistBuff, ID3D12Resource* TempBuff = nullptr)
#else
	void Bind(IDMLOperatorInitializer* OpInit, TConstArrayView<FRHIBuffer*> InputBuffers, FRHIBuffer* PersistBuff, FRHIBuffer* TempBuff = nullptr)
#endif
	{
		Reset(OpInit);

		TArray<DML_BUFFER_BINDING, TInlineAllocator<MaxNumInputs>> Inputs;

		for (FRHIBuffer* Buffer : InputBuffers)
		{
			if (Buffer)
			{
				Inputs.Emplace(MakeBind(Buffer));
			}
			else
			{
				Inputs.Add({});
			}
		}

		DML_BUFFER_ARRAY_BINDING	InputBindArray{ Inputs.Num(), Inputs.GetData() };
		DML_BINDING_DESC			InputBindArrayDesc{ DML_BINDING_TYPE_BUFFER_ARRAY, &InputBindArray };
		
		BindingTable->BindInputs(1, &InputBindArrayDesc);
		
		DML_BUFFER_BINDING			PersistBind {};
		DML_BINDING_DESC			PersistBindDesc{ DML_BINDING_TYPE_BUFFER, &PersistBind };

		if (PersistBuff)
		{
#ifdef NNE_USE_D3D12_RESOURCES
			PersistBind = DML_BUFFER_BINDING { PersistBuff, 0, PersistBuff->GetDesc().Width };
#else
			PersistBind = MakeBind(PersistBuff);
#endif
		}

		BindingTable->BindOutputs(1, &PersistBindDesc);

		DML_BUFFER_BINDING			TempBind{};
		DML_BINDING_DESC			TempBindDesc{ DML_BINDING_TYPE_BUFFER, &TempBind };

		if (TempBuff)
		{
#ifdef NNE_USE_D3D12_RESOURCES
			TempBind = { PersistBuff, 0, PersistBuff->GetDesc().Width };
#else
			TempBind = MakeBind(TempBuff);
#endif
			BindingTable->BindTemporaryResource(&TempBindDesc);
		}
	}

#ifdef NNE_USE_D3D12_RESOURCES
	void Bind(IDMLCompiledOperator* Op, TConstArrayView<FRHIBuffer*> InputBuffers, TConstArrayView<FRHIBuffer*> OutputBuffers, ID3D12Resource* PersistBuff = nullptr, ID3D12Resource* TempBuff = nullptr)
#else
	
	void Bind(IDMLCompiledOperator* Op, TConstArrayView<FRHIBuffer*> InputBuffers, TConstArrayView<FRHIBuffer*> OutputBuffers, FRHIBuffer* PersistBuff = nullptr, FRHIBuffer* TempBuff = nullptr)
#endif
	{
		Reset(Op);

		for (FRHIBuffer* Buffer : InputBuffers)
		{
			AddBind(Buffer, InputBinds, InputBindDescs);
		}

		for (FRHIBuffer* Buffer : OutputBuffers)
		{
			AddBind(Buffer, OutputBinds, OutputBindDescs);
		}

		BindingTable->BindInputs(InputBinds.Num(), InputBindDescs.GetData());
		BindingTable->BindOutputs(OutputBinds.Num(), OutputBindDescs.GetData());

		DML_BUFFER_BINDING			PersistBind;
		DML_BINDING_DESC			PersistBindDesc{ DML_BINDING_TYPE_BUFFER, &PersistBind };

		if (PersistBuff)
		{
#ifdef NNE_USE_D3D12_RESOURCES
			PersistBind = { PersistBuff, 0, PersistBuff->GetDesc().Width };
#else
			PersistBind =  MakeBind(PersistBuff);MakeBind(PersistBuff);
#endif
			BindingTable->BindPersistentResource(&PersistBindDesc);
		}

		DML_BUFFER_BINDING			TempBind{};
		DML_BINDING_DESC			TempBindDesc{ DML_BINDING_TYPE_BUFFER, &TempBind };

		if (TempBuff)
		{
			
#ifdef NNE_USE_D3D12_RESOURCES
			TempBind = { TempBuff, 0, TempBuff->GetDesc().Width };
#else
			TempBind = MakeBind(TempBuff);
#endif
			BindingTable->BindTemporaryResource(&TempBindDesc);
		}
	}


	IDMLBindingTable* Get()
	{
		return BindingTable;
	}

private:

	bool Reset(IDMLDispatchable* Disp)
	{
		InputBinds.Reset();
		InputBindDescs.Reset();
		OutputBinds.Reset();
		OutputBindDescs.Reset();
		
		DML_BINDING_PROPERTIES BindingProps = Disp->GetBindingProperties();
		DML_BINDING_TABLE_DESC Desc{};

		Desc.Dispatchable = Disp;
		Desc.CPUDescriptorHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(Model->DescHeap->GetCPUDescriptorHandleForHeapStart(), 0, Model->DescSize);
		Desc.GPUDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(Model->DescHeap->GetGPUDescriptorHandleForHeapStart(), 0, Model->DescSize);
		Desc.SizeInDescriptors = Model->DescCount;

		HRESULT Res;

		if (!BindingTable)
		{
			Res = Model->DevCtx->Device->CreateBindingTable(&Desc, DML_PPV_ARGS(&BindingTable));
			if (!BindingTable)
			{
				UE_LOG(LogNNX, Warning, TEXT("Failed to create DML binding table, res:%d"), Res);
				return false;
			}
		}
		else
		{
			BindingTable->Reset(&Desc);
		}

		return true;
	}

	template<class TBindingArray, class TDescArray>
	void AddBind(FRHIBuffer* Buffer, TBindingArray& Bindings, TDescArray& Descs)
	{
		if (Buffer)
		{
			DML_BUFFER_BINDING& Bind = Bindings.Add_GetRef(MakeBind(Buffer));
			Descs.Add({ DML_BINDING_TYPE_BUFFER, &Bind });
		}
		else
		{
			DML_BUFFER_BINDING& Bind = Bindings.Add_GetRef({});
			Descs.Add({ DML_BINDING_TYPE_BUFFER, &Bind });
		}
	}

	DML_BUFFER_BINDING MakeBind(FRHIBuffer* Buffer)
	{
		ID3D12Resource* Resource = DynamicRHI->RHIGetResource(Buffer);

		return DML_BUFFER_BINDING{ Resource, 0, Buffer->GetSize() };
	}

	TComPtr<IDMLBindingTable>										BindingTable;
	TArray<DML_BUFFER_BINDING, TInlineAllocator<MaxNumInputs>>		InputBinds;
	TArray<DML_BINDING_DESC, TInlineAllocator<MaxNumInputs>>		InputBindDescs;
	TArray<DML_BUFFER_BINDING, TInlineAllocator<MaxNumOutputs>>		OutputBinds;
	TArray<DML_BINDING_DESC, TInlineAllocator<MaxNumOutputs>>		OutputBindDescs;
	ID3D12DynamicRHI*												DynamicRHI;
	FMLInferenceModelDml*											Model;
};

//
//
//
class FMLInferenceModelDml::FGraphBuilder
{
public:

	struct FOpDesc
	{
		FMLOperatorDml* Op;
		int32			InputStart;
		int32			InputCount;
		int32			OutputStart;
		int32			OutputCount;
		FDebugName		DbgName;
	};

	struct FGraphDesc
	{
		TConstArrayView<UE::NNECore::Internal::FTensor>	AllTensors;
		TConstArrayView<int32>		InputIndices;
		TConstArrayView<int32>		OutputIndices;
		TConstArrayView<int32>		WeightIndices;
		TConstArrayView<int32>		IntermediateIndices;
		TConstArrayView<FTensorRDG>	WeightTensors;
		TConstArrayView<FOpDesc>	Operators;
		TConstArrayView<int32>		OpInputIndices;
		TConstArrayView<int32>		OpOutputIndices;
	};
	
private:

	enum class EEdgeType
	{
		Input,
		Output,
		Intermediate
	};

	struct FEdge
	{
		EEdgeType	Type;
		int32		TensorIdx{ -1 };
		int32		NodeSrc{ -1 };
		int32		NodeSrcOutput{ -1 };
		int32		NodeDst{ -1 };
		int32		NodeDstInput{ -1 };
		
		FEdge(EEdgeType InType)
			: Type(InType)
		{
		}

		FEdge& SetTensorIdx(int32 Value)
		{
			TensorIdx = Value;
			return *this;
		}

		FEdge& SetNodeSrc(int32 Value)
		{
			NodeSrc = Value;
			return *this;
		}

		FEdge& SetNodeSrcOutput(int32 Value)
		{
			NodeSrcOutput = Value;
			return *this;
		}

		FEdge& SetNodeDst(int32 Value)
		{
			NodeDst = Value;
			return *this;
		}

		FEdge& SetNodeDstInput(int32 Value)
		{
			NodeDstInput = Value;
			return *this;
		}
	};

public:

	IDMLCompiledOperator* Compile(FDeviceContextDml* DevCtx, const FGraphDesc& InGraph)
	{
		IDMLDevice*				Device = DevCtx->Device;
		TComPtr<IDMLDevice1>	Device1;

		Device1.FromQueryInterface(__uuidof(IDMLDevice1), DevCtx->Device);
		check(Device1);
		if (!Device1)
		{
			return nullptr;
		}

		if (!AddEdges(InGraph))
		{
			return nullptr;
		}

		TArray<DML_INPUT_GRAPH_EDGE_DESC>			InputEdges;
		TArray<DML_OUTPUT_GRAPH_EDGE_DESC>			OutputEdges;
		TArray<DML_INTERMEDIATE_GRAPH_EDGE_DESC>	IntermediateEdges;
		TArray<FDebugName>							DbgInputNames;
		TArray<FDebugName>							DbgIntermediateNames;
		TArray<FDebugName>							DbgOutputNames;

		DbgInputNames.Reserve(InGraph.AllTensors.Num());
		DbgIntermediateNames.Reserve(InGraph.AllTensors.Num());

		for (const FEdge& Edge : Edges)
		{
			if (Edge.Type == EEdgeType::Input)
			{
				check(Edge.NodeSrcOutput >= 0);
				check(Edge.NodeDst >= 0);
				check(Edge.NodeDstInput >= 0);

				DML_INPUT_GRAPH_EDGE_DESC& Input = InputEdges.Add_GetRef({});

				Input.GraphInputIndex = Edge.NodeSrcOutput;
				Input.ToNodeIndex = Edge.NodeDst;
				Input.ToNodeInputIndex = Edge.NodeDstInput;
				
				const FDebugName& DbgName = DbgInputNames.Add_GetRef(InGraph.AllTensors[Edge.TensorIdx].GetName());
				Input.Name = DbgName.Get();
			}
			else if (Edge.Type == EEdgeType::Output)
			{
				check(Edge.NodeDstInput >= 0);
				check(Edge.NodeSrc >= 0);
				check(Edge.NodeSrcOutput >= 0);

				DML_OUTPUT_GRAPH_EDGE_DESC&	Output = OutputEdges.Add_GetRef({});

				Output.GraphOutputIndex = Edge.NodeDstInput;
				Output.FromNodeIndex = Edge.NodeSrc;
				Output.FromNodeOutputIndex = Edge.NodeSrcOutput;

				const FDebugName& DbgName = DbgOutputNames.Add_GetRef(InGraph.AllTensors[Edge.TensorIdx].GetName());
				Output.Name = DbgName.Get();
			}
			else if (Edge.Type == EEdgeType::Intermediate)
			{
				DML_INTERMEDIATE_GRAPH_EDGE_DESC& Intermediate = IntermediateEdges.Add_GetRef({});

				check(Edge.NodeSrc >= 0);
				check(Edge.NodeSrcOutput >= 0);
				check(Edge.NodeDst >= 0);
				check(Edge.NodeDstInput >= 0);

				Intermediate.FromNodeIndex = Edge.NodeSrc;
				Intermediate.FromNodeOutputIndex = Edge.NodeSrcOutput;
				Intermediate.ToNodeIndex = Edge.NodeDst;
				Intermediate.ToNodeInputIndex = Edge.NodeDstInput;

				const FDebugName& DbgName = DbgIntermediateNames.Add_GetRef(InGraph.AllTensors[Edge.TensorIdx].GetName());
				Intermediate.Name = DbgName.Get();
			}
		}
			
		TArray<DML_GRAPH_NODE_DESC>		Nodes;
		TArray<DML_GRAPH_EDGE_DESC>		InputEdgeDescs;
		TArray<DML_GRAPH_EDGE_DESC>		OutputEdgeDescs;
		TArray<DML_GRAPH_EDGE_DESC>		IntermediateEdgeDescs;

		Nodes.SetNumUninitialized(Operators.Num());

		for (int32 Idx = 0; Idx < Operators.Num(); ++Idx)
		{
			DML_GRAPH_NODE_DESC& NodeDesc = Nodes[Idx];

			NodeDesc.Type = DML_GRAPH_NODE_TYPE_OPERATOR;
			NodeDesc.Desc = &Operators[Idx];
		}

		for (int32 Idx = 0; Idx < InputEdges.Num(); ++Idx)
		{
			DML_GRAPH_EDGE_DESC& Edge = InputEdgeDescs.Add_GetRef({});

			Edge.Type = DML_GRAPH_EDGE_TYPE_INPUT;
			Edge.Desc = &InputEdges[Idx];
		}			

		for (int32 Idx = 0; Idx < OutputEdges.Num(); ++Idx)
		{
			DML_GRAPH_EDGE_DESC& Edge = OutputEdgeDescs.Add_GetRef({});

			Edge.Type = DML_GRAPH_EDGE_TYPE_OUTPUT;
			Edge.Desc = &OutputEdges[Idx];				
		}

		for (int32 Idx = 0; Idx < IntermediateEdges.Num(); ++Idx)
		{
			DML_GRAPH_EDGE_DESC& Edge = IntermediateEdgeDescs.Add_GetRef({});

			Edge.Type = DML_GRAPH_EDGE_TYPE_INTERMEDIATE;
			Edge.Desc = &IntermediateEdges[Idx];
		}
			
		DML_GRAPH_DESC	Graph = DML_GRAPH_DESC{};

		Graph.InputCount = InputEdges.Num();
		Graph.OutputCount = OutputEdges.Num();
		Graph.NodeCount = Operators.Num();
		Graph.Nodes = Nodes.GetData();
		Graph.InputEdgeCount = InputEdgeDescs.Num();
		Graph.InputEdges = InputEdgeDescs.GetData();
		Graph.OutputEdgeCount = OutputEdgeDescs.Num();
		Graph.OutputEdges = OutputEdgeDescs.GetData();
		Graph.IntermediateEdgeCount = IntermediateEdgeDescs.Num();
		Graph.IntermediateEdges = IntermediateEdgeDescs.GetData();

		IDMLCompiledOperator* Op = nullptr;
		HRESULT Res;
			
		Res = Device1->CompileGraph(&Graph, DML_EXECUTION_FLAG_NONE, DML_PPV_ARGS(&Op));
		if (FAILED(Res))
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:Failed to compile DML graph"));
			Op = nullptr;
		};

		return Op;
	}

private:

	bool AddEdges(const FGraphDesc& InGraph)
	{
		Edges.Reset();
		Operators.Reset();
		NumInputs = 0;
		NumOutputs = 0;

		for (int32 Idx = 0; Idx < InGraph.InputIndices.Num(); ++Idx)
		{
			const int32 TensorIdx = InGraph.InputIndices[Idx];
			AddInput(TensorIdx);
		}

		for (int32 Idx = 0; Idx < InGraph.WeightIndices.Num(); ++Idx)
		{
			const int32 TensorIdx = InGraph.WeightIndices[Idx];
			AddInput(TensorIdx);
		}

		//const int32 OutputIndexStart = InGraph.InputIndices.Num() + InGraph.WeightIndices.Num() + InGraph.IntermediateIndices.Num();

		for (int32 Idx = 0; Idx < InGraph.OutputIndices.Num(); ++Idx)
		{
			const int32 TensorIdx = InGraph.OutputIndices[Idx];
			AddOutput(TensorIdx);
		}

		Operators.Reset(InGraph.Operators.Num());
		for (const FOpDesc& OpDesc : InGraph.Operators)
		{
			AddOp(OpDesc, InGraph);
		}

		// TODO: FIXME: Validate edges here
		
		return true;
	}

	void AddInput(int32 TensorIdx)
	{
		AddEdge(
			FEdge(EEdgeType::Input)
				.SetTensorIdx(TensorIdx)
				.SetNodeSrcOutput(NumInputs)
		);

		++NumInputs;
	}

	void AddOutput(int32 TensorIdx)
	{
		AddEdge(
			FEdge(EEdgeType::Output)
				.SetTensorIdx(TensorIdx)
				.SetNodeDstInput(NumOutputs)
		);

		++NumOutputs;
	}

	void AddIntermediate(int32 TensorIdx, int32 NodeSrc, int32 NodeSrcOutput)
	{
		FEdge* ConnEdge = Edges.FindByPredicate(
				[TensorIdx](const FEdge& Curr)
				{
					return Curr.TensorIdx == TensorIdx;
				}
		);

		if (ConnEdge)
		{
			ConnectEdgeSrc(TensorIdx, NodeSrc, NodeSrcOutput);
		}
		else
		{
			AddEdge(
				FEdge(EEdgeType::Intermediate)
					.SetTensorIdx(TensorIdx)
					.SetNodeSrc(NodeSrc)
					.SetNodeSrcOutput(NodeSrcOutput)
			);
		}
	}

	void AddEdge(const FEdge& Edge)
	{
		FEdge* StartEdge = Edges.FindByPredicate(
				[Edge](const FEdge& Curr)
				{
					return Curr.TensorIdx == Edge.TensorIdx;
				}
		);

		check(StartEdge == nullptr);
		Edges.Add(Edge);
	}

	bool ConnectEdgeDst(int32 TensorIdx, int32 NodeDst, int32 NodeDstInput)
	{
		FEdge* StartEdge = Edges.FindByPredicate(
				[TensorIdx](const FEdge& Curr)
				{
					return Curr.TensorIdx == TensorIdx;
				}
		);

		bool bFoundEdge = false;

		for (FEdge* Curr = StartEdge; Curr->TensorIdx == TensorIdx; ++Curr)
		{
			if (Curr->NodeDst == -1 && Curr->NodeDstInput == -1)
			{
				Curr->NodeDst = NodeDst;
				Curr->NodeDstInput = NodeDstInput;
				bFoundEdge = true;
				break;
			}
			else if (Curr->NodeDst == NodeDst && Curr->NodeDstInput == NodeDstInput)
			{
				bFoundEdge = true;
				break;
			}
		}

		checkf(bFoundEdge, TEXT("ConnectEdgeDst() has failed"));
		return bFoundEdge;
	}

	bool ConnectEdgeSrc(int32 TensorIdx, int32 NodeSrc, int32 NodeSrcOutput)
	{
		FEdge* StartEdge =
			Edges.FindByPredicate(
				[TensorIdx](const FEdge& Curr)
				{
					return Curr.TensorIdx == TensorIdx;
				}
		);

		bool bFoundEdge = false;

		for (FEdge* Curr = StartEdge; Curr->TensorIdx == TensorIdx; ++Curr)
		{
			if (Curr->NodeSrc == -1 && Curr->NodeSrcOutput == -1)
			{
				Curr->NodeSrc = NodeSrc;
				Curr->NodeSrcOutput = NodeSrcOutput;
				bFoundEdge = true;
				break;
			}
			else if (Curr->NodeSrc == NodeSrc && Curr->NodeSrcOutput == NodeSrcOutput)
			{
				bFoundEdge = true;
				break;
			}
		}

		checkf(bFoundEdge, TEXT("ConnectEdgeSrc() has failed"));
		return bFoundEdge;
	}

	void AddOp(const FOpDesc& InOp, const FGraphDesc& InGraph)
	{
		DML_OPERATOR_GRAPH_NODE_DESC& OpDesc = Operators.Add_GetRef({});

		OpDesc.Operator = InOp.Op->GetOperator();
		OpDesc.Name = InOp.DbgName.Get();

		const int32 NodeIdx = Operators.Num() - 1;

		for (int32 Idx = 0; Idx < InOp.InputCount; ++Idx)
		{
			const int32 TensorIdx = InGraph.OpInputIndices[Idx + InOp.InputStart];				
			
			ConnectEdgeDst(TensorIdx, NodeIdx, Idx);
		}

		for (int32 Idx = 0; Idx < InOp.OutputCount; ++Idx)
		{
			const int32 TensorIdx = InGraph.OpOutputIndices[Idx + InOp.OutputStart];

			AddIntermediate(TensorIdx, NodeIdx, Idx);
		}
	}

	TArray<FEdge>							Edges;
	TArray<DML_OPERATOR_GRAPH_NODE_DESC>	Operators;
	int32									NumInputs;
	int32									NumOutputs;
};

//
//
//
FMLInferenceModelDml::FMLInferenceModelDml()
{
	bUseManualTransitions = true;
}

//
//
//
FMLInferenceModelDml::~FMLInferenceModelDml()
{
}

//
//
//
bool FMLInferenceModelDml::Init(TConstArrayView<uint8> ModelData, FDeviceContextDml* InDevCtx)
{
	check(ModelData.Num() > 0);
	FMLRuntimeFormat	Format;

	if (!LoadModel(ModelData, Format))
	{
		return false;
	}

	DevCtx = InDevCtx;
	DynamicRHI = GetID3D12PlatformDynamicRHI();

	HRESULT Res = DevCtx->Device->CreateOperatorInitializer(0, nullptr, DML_PPV_ARGS(&OpInit));
	if (!OpInit)
	{
		UE_LOG(LogNNX, Warning, TEXT("Error:Failed to create DML operator initializer"));
		return false;
	}

	// DirectML requires all tensors to be concrete
	// TODO jira 168972: Handle dynamic tensor desc, op should init from symbolic shapes
	TArray<UE::NNECore::Internal::FTensor>	Tensors;

	Tensors.Reset(AllSymbolicTensorDescs.Num());
	for (const UE::NNECore::FTensorDesc& TensorDesc : AllSymbolicTensorDescs)
	{
		Tensors.Emplace(UE::NNECore::Internal::FTensor::MakeFromSymbolicDesc(TensorDesc));
	}

	FGraphBuilder				DmlGraphBuilder;
	FGraphBuilder::FGraphDesc	DmlGraphDesc;

	DmlGraphDesc.AllTensors = Tensors;
	DmlGraphDesc.InputIndices = InputTensorIndices;
	DmlGraphDesc.OutputIndices = OutputTensorIndices;
	DmlGraphDesc.WeightIndices = WeightTensorIndices;
	DmlGraphDesc.IntermediateIndices = IntermediateTensorIndices;
	DmlGraphDesc.WeightTensors = WeightTensorRDGs;
	
	TArray<FGraphBuilder::FOpDesc>	DmlGraphOperators;
	TArray<int32>					OpInputIndices;
	TArray<int32>					OpOutputIndices;
	uint64							TensorDataSize = 0;

	// Loop over all operators in the model and create them
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		const FString TypeName = Format.Operators[Idx].TypeName;

		FGraphBuilder::FOpDesc		OpDesc;
		TArray<UE::NNECore::Internal::FTensor>				OpInputTensors;
		TArray<UE::NNECore::Internal::FTensor>				OpOutputTensors;
		UE::NNECore::FAttributeMap	AttributeMap;

		OpDesc.InputStart = OpInputIndices.Num();
		OpDesc.OutputStart = OpOutputIndices.Num();

		for (int32 InputTensorIndex : Format.Operators[Idx].InTensors)
		{
			const int32 WeightTensorIdx = WeightTensorIndices.Find(InputTensorIndex);

			if (WeightTensorIdx >= 0)
			{
				TConstArrayView<uint8> TensorData = OpInputTensors.Emplace_GetRef(WeightTensorRDGs[WeightTensorIdx]).GetPreparedData<uint8>();
				
				TensorDataSize += Align(TensorData.Num(), DML_MINIMUM_BUFFER_TENSOR_ALIGNMENT);
			}
			else
			{
				FTensorDesc SymbolicTensorDesc = AllSymbolicTensorDescs[InputTensorIndex];
				//TODO jira 168972: Handle dynamic tensor desc, op should init from symbolic shapes
				OpInputTensors.Emplace(UE::NNECore::Internal::FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc));
			}

			OpInputIndices.Emplace(InputTensorIndex);
		}

		for (int32 OutputTensorIndex : Format.Operators[Idx].OutTensors)
		{
			FTensorDesc SymbolicTensorDesc = AllSymbolicTensorDescs[OutputTensorIndex];
			//TODO jira 168972: Handle dynamic tensor desc, op should init from symbolic shapes
			OpOutputTensors.Emplace(UE::NNECore::Internal::FTensor::MakeFromSymbolicDesc(SymbolicTensorDesc));
			OpOutputIndices.Emplace(OutputTensorIndex);
		}

		for (const FMLFormatAttributeDesc& Desc : Format.Operators[Idx].Attributes)
		{
			AttributeMap.SetAttribute(Desc.Name, Desc.Value);
		}

		OpDesc.Op = OpCreate(TypeName, OpInputTensors, OpOutputTensors, AttributeMap);

		if (!OpDesc.Op)
		{
			UE_LOG(LogNNX, Warning, TEXT("Error:Failed to create operator:%s"), *TypeName);
			return false;
		}

		OpDesc.InputCount = OpInputTensors.Num();
		OpDesc.OutputCount = OpOutputTensors.Num();
		OpDesc.DbgName = TypeName;

		DmlGraphOperators.Emplace(OpDesc);
	}

	DmlGraphDesc.Operators = DmlGraphOperators;
	DmlGraphDesc.OpInputIndices = OpInputIndices;
	DmlGraphDesc.OpOutputIndices = OpOutputIndices;

	CompiledOp = DmlGraphBuilder.Compile(DevCtx, DmlGraphDesc);
	if (!CompiledOp)
	{
		return false;
	}

	return InitCompiledOp(OpInputIndices, TensorDataSize);
}

//
//
//
bool FMLInferenceModelDml::InitCompiledOp(TConstArrayView<int32> OpInputIndices, uint64 TensorDataSize)
{
	static constexpr EBufferUsageFlags	WeightBuffUsage = BUF_UnorderedAccess;
	static constexpr ERHIAccess			WeightBuffAccess = ERHIAccess::UAVMask;

	static constexpr EBufferUsageFlags	PersistBuffFlags = BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess;
	static constexpr ERHIAccess			PersistBuffAccess = ERHIAccess::UAVMask;

	static constexpr EBufferUsageFlags	TempBuffFlags = BUF_Volatile | BUF_UnorderedAccess;
	static constexpr ERHIAccess			TempBuffAccess = ERHIAccess::UAVMask;

	HRESULT					Res;
	IDMLDevice*				Device = DevCtx->Device;
	IDMLCompiledOperator*	CompiledOps[] = { CompiledOp };
	
	Res = OpInit->Reset(UE_ARRAY_COUNT(CompiledOps), CompiledOps);
	if (FAILED(Res))
	{
		UE_LOG(LogNNX, Warning, TEXT("Error:Failed to reset DirectML operator initializer"));
		return false;
	}

	DML_BINDING_PROPERTIES InitBindProps = OpInit->GetBindingProperties();
	DML_BINDING_PROPERTIES ExecBindProps = CompiledOp->GetBindingProperties();

	DescCount = std::max(InitBindProps.RequiredDescriptorCount, ExecBindProps.RequiredDescriptorCount);
	
	D3D12_DESCRIPTOR_HEAP_DESC	HeapDesc = {};

	HeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	HeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	HeapDesc.NumDescriptors = DescCount;

	Res = DevCtx->D3D12Device->CreateDescriptorHeap(&HeapDesc, IID_PPV_ARGS(&DescHeap));
	if (!DescHeap)
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create descriptor heap, res:%x"), Res);
		return false;
	}

	DescSize = DevCtx->D3D12Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	BindingTable.Reset(new FBindingTable());
	if (!BindingTable->Init(this))
	{
		return false;
	}

	MemSizeTemp = ExecBindProps.TemporaryResourceSize;
	MemSizePersist = ExecBindProps.PersistentResourceSize;

	FEvent* Signal = FGenericPlatformProcess::GetSynchEventFromPool(false);

	ENQUEUE_RENDER_COMMAND(FMLInferenceModelDml_SetTensorData)
	(
		[
			this, 
			Signal, 
			InitTempMemSize = InitBindProps.TemporaryResourceSize,
			TensorDataSize
		]
		(FRHICommandListImmediate& RHICmdList)
		{
			FRHIBufferInputArray	Inputs;

			for (int32 InputIdx : InputTensorIndices)
			{
				Inputs.Emplace(nullptr);
			}

			TArray<CD3DX12_RESOURCE_BARRIER, TInlineAllocator<MaxNumInputs>>	Barriers;
			FGPUFenceRHIRef	UploadFence = nullptr;

			if (TensorDataSize)
			{
				UploadFence = RHICmdList.CreateGPUFence(TEXT("FMLInferenceModel_UploadFence"));
				
				FRHIBuffer*		UploadBuff = CreateRHIBuffer(RHICmdList, TensorDataSize, BUF_ShaderResource | BUF_Dynamic | BUF_FastVRAM, ERHIAccess::CopySrc, TEXT("FMLInferenceModel_UploadBuffer"));
				uint8*			UploadBuffPtr = static_cast<uint8*>(RHICmdList.LockBuffer(UploadBuff, 0, TensorDataSize, RLM_WriteOnly_NoOverwrite));
				uint64			UploadOffset = 0;

				for (const FTensorRDG& Tensor : WeightTensorRDGs)
				{
					TConstArrayView<uint8>	TensorData = Tensor.GetPreparedData<uint8>();

					FBufferRHIRef WeightBuff;

					WeightBuff = CreateRHIBuffer(RHICmdList, TensorData.Num(), WeightBuffUsage, WeightBuffAccess, TEXT("FMLInferenceModelDml_TensorWeights"));
				
					FMemory::Memcpy(UploadBuffPtr + UploadOffset, TensorData.GetData(), TensorData.Num());
					RHICmdList.CopyBufferRegion(WeightBuff, 0, UploadBuff, UploadOffset, TensorData.Num());
					UploadOffset += Align(TensorData.Num(), DML_MINIMUM_BUFFER_TENSOR_ALIGNMENT);

					Inputs.Emplace(WeightBuff);
				
					Barriers.Emplace(
						CD3DX12_RESOURCE_BARRIER::Transition(
							DynamicRHI->RHIGetResource(WeightBuff),
							D3D12_RESOURCE_STATE_COPY_DEST,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
					);
				}

				RHICmdList.UnlockBuffer(UploadBuff);
				RHICmdList.WriteGPUFence(UploadFence);
			}

			if (MemSizePersist)
			{
#ifdef NNE_USE_D3D12_RESOURCES
				PersistBuff = CreateD3D12Buffer(MemSizePersist);
#else		
				PersistBuff = CreateRHIBuffer(RHICmdList, MemSizePersist, PersistBuffFlags, PersistBuffAccess, TEXT("FMLInferendeModelDml_PeristBuff"));
#endif
			}

			if (MemSizeTemp)
			{
#ifdef NNE_USE_D3D12_RESOURCES
				TempBuff = CreateD3D12Buffer(MemSizeTemp);
#else
				TempBuff = CreateRHIBuffer(RHICmdList, MemSizeTemp, TempBuffFlags, TempBuffAccess, TEXT("FMLInferendeModelDml_TempBuff"));
#endif
			}

#ifdef NNE_USE_D3D12_RESOURCES
			ID3D12Resource* InitTempBuff = nullptr;
#else
			FBufferRHIRef InitTempBuff;
#endif

			if (InitTempMemSize)
			{
#ifdef NNE_USE_D3D12_RESOURCES
				InitTempBuff = CreateD3D12Buffer(InitTempMemSize);
#else
				TempBuff = CreateRHIBuffer(RHICmdList, InitTempMemSize, TempBuffFlags, TempBuffAccess, TEXT("FMLInferendeModelDml_InitTempBuff"));
#endif
			}

			RHICmdList.EnqueueLambda(
				[this, Inputs, Barriers, InitTempBuff, UploadFence](FRHICommandListImmediate& RHICmdList)
				{
					while (UploadFence && UploadFence->NumPendingWriteCommands.GetValue() > 0)
					{
						FPlatformProcess::Sleep(0.001);
					}

					ID3D12GraphicsCommandList* D3DCmdList = nullptr;

					D3DCmdList = DynamicRHI->RHIGetGraphicsCommandList(DevCtx->DeviceIndex);

					BindingTable->Bind(OpInit, Inputs, PersistBuff, InitTempBuff);
			
					D3DCmdList = DynamicRHI->RHIGetGraphicsCommandList(DevCtx->DeviceIndex);

					if (!Barriers.IsEmpty())
					{
						D3DCmdList->ResourceBarrier(Barriers.Num(), Barriers.GetData());
					}
					D3DCmdList->SetDescriptorHeaps(1, &DescHeap);
					DevCtx->CmdRec->RecordDispatch(D3DCmdList, OpInit, BindingTable->Get());

					DynamicRHI->RHIFinishExternalComputeWork(DevCtx->DeviceIndex, D3DCmdList);
				}
			);			
			
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);
			Signal->Trigger();
		}
	);

	Signal->Wait();
	FGenericPlatformProcess::ReturnSynchEventToPool(Signal);

	return true;
}

BEGIN_SHADER_PARAMETER_STRUCT(FTensorBufferParamsDml, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::UAVCompute)
END_SHADER_PARAMETER_STRUCT()

//
//
//
void FMLInferenceModelDml::AddDispatchOps_RenderThread(FRDGBuilder& GraphBuilder)
{
	const ERDGPassFlags TransitionBuffFlags = ERDGPassFlags::Compute | ERDGPassFlags::NeverCull;

	InputBuffers.SetNumUninitialized(InputTensorIndices.Num() + WeightTensorIndices.Num());
	OutputBuffers.SetNumUninitialized(OutputTensorIndices.Num());

	for (int32 Idx = 0; Idx < InputTensorIndices.Num(); ++Idx)
	{
		FTensorBufferParamsDml* Params = GraphBuilder.AllocParameters<FTensorBufferParamsDml>();
		Params->Buffer = AllTensorRDGs[InputTensorIndices[Idx]]->GetBuffer();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLInferenceModelDml_Dispatch_GetInputBuffer"),
			Params,
			TransitionBuffFlags,
			[this, Idx, Params](FRHICommandListImmediate& RHICmdList)
			{
				InputBuffers[Idx] = Params->Buffer->GetRHI();
			}
		);
	}

	for (int32 Idx = 0; Idx < WeightTensorIndices.Num(); ++Idx)
	{
		InputBuffers[Idx + InputTensorIndices.Num()] = nullptr;
	}

	for (int32 Idx = 0; Idx < OutputTensorIndices.Num(); ++Idx)
	{
		FTensorBufferParamsDml* Params = GraphBuilder.AllocParameters<FTensorBufferParamsDml>();
		Params->Buffer = AllTensorRDGs[OutputTensorIndices[Idx]]->GetBuffer();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLInferenceModelDml_Dispatch_GetOutputBuffer"),
			Params,
			TransitionBuffFlags,
			[this, Idx, Params](FRHICommandListImmediate& RHICmdList)
			{
				OutputBuffers[Idx] = Params->Buffer->GetRHI();
			}
		);
	}
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("FMLInferenceModelDml_Dispatch"),
		ERDGPassFlags::None | ERDGPassFlags::NeverCull,
		[this](FRHICommandListImmediate& RHICmdList)
		{
			RHICmdList.EnqueueLambda(
				[this](FRHICommandListImmediate& RHICmdList)
				{
					TArray<CD3DX12_RESOURCE_BARRIER, TInlineAllocator<MaxNumInputs + MaxNumOutputs>>	Barriers;

					for (FRHIBuffer* Buffer : InputBuffers)
					{
						if (!Buffer)
						{
							continue;
						}

						ID3D12Resource* Resource = DynamicRHI->RHIGetResource(Buffer);

						// TODO: FIXME: Don't assume COPY_DEST state
						Barriers.Emplace(
							CD3DX12_RESOURCE_BARRIER::Transition(
								Resource,
								D3D12_RESOURCE_STATE_COPY_DEST,
								D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
						);
					}

					// TODO: We should use this instead of NNE_USE_D3D12_RESOURCES
					//FBufferRHIRef TempBuff = MemSizeTemp ? CreateRHIBuffer(RHICmdList, MemSizeTemp, TempBuffUsage, TempBuffAccess, TEXT("FMLInferenceModelDml_Dispatch_TempBuff")) : nullptr;

					BindingTable->Bind(CompiledOp, InputBuffers, OutputBuffers, PersistBuff, TempBuff);

					ID3D12GraphicsCommandList* D3DCmdList = nullptr;

					D3DCmdList = DynamicRHI->RHIGetGraphicsCommandList(DevCtx->DeviceIndex);
					D3DCmdList->SetDescriptorHeaps(1, &DescHeap);
					D3DCmdList->ResourceBarrier(Barriers.Num(), Barriers.GetData());
					DevCtx->CmdRec->RecordDispatch(D3DCmdList, CompiledOp, BindingTable->Get());

					DynamicRHI->RHIFinishExternalComputeWork(DevCtx->DeviceIndex, D3DCmdList);
				}
			);
		}
	);
}

//
// Create operator
//
FMLOperatorDml* FMLInferenceModelDml::OpCreate(const FString& OpName, TArrayView<const UE::NNECore::Internal::FTensor> InputTensorDescs, TArrayView<const UE::NNECore::Internal::FTensor> OutputTensorDescs, const UE::NNECore::FAttributeMap& Attributes)
{
	FMLOperatorRegistryDml::OperatorCreateFunc CreateFn = FMLOperatorRegistryDml::Get()->OpFind(OpName);

	if (!CreateFn)
	{
		UE_LOG(LogNNX, Warning, TEXT("Dml MLOperatorRegistry failed to find operator:%s"), *OpName);
		return nullptr;
	}

	FMLOperatorDml* Op = CreateFn();

	if (!Op->Initialize(DevCtx, InputTensorDescs, OutputTensorDescs, Attributes))
	{
		delete Op;

		UE_LOG(LogNNX, Warning, TEXT("Error:Failed to initialize operator:%s"), *OpName);
		return nullptr;
	}

	Op->GetOperator()->SetName(*OpName);

	return Op;
}

FBufferRHIRef FMLInferenceModelDml::CreateRHIBuffer(FRHICommandListImmediate& RHICmdList, uint32 Size, EBufferUsageFlags Usage, ERHIAccess Access, const TCHAR* DbgName)
{
	FBufferRHIRef Buff = nullptr;

	if (Size)
	{
		FRHIResourceCreateInfo CreateInfo(DbgName);
		
		Buff = RHICmdList.CreateBuffer(Size, Usage, 1, Access, CreateInfo);
	}

	check(Buff);
	return Buff;
}

ID3D12Resource* FMLInferenceModelDml::CreateD3D12Buffer(uint32 Size, D3D12_RESOURCE_STATES ResourceState, D3D12_HEAP_TYPE HeapType, const TCHAR* DebugName)
{
	ID3D12Resource* Resource = nullptr;

	D3D12_RESOURCE_DESC ResourceDesc = CD3DX12_RESOURCE_DESC::Buffer(Size,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	CD3DX12_HEAP_PROPERTIES HeapProps = CD3DX12_HEAP_PROPERTIES(HeapType);
	HRESULT Res;

	Res = DevCtx->D3D12Device->CreateCommittedResource(
		&HeapProps,
		D3D12_HEAP_FLAG_NONE,
		&ResourceDesc,
		ResourceState,
		nullptr,
		IID_PPV_ARGS(&Resource));

	if (FAILED(Res))
	{
		UE_LOG(LogNNX, Warning, TEXT("Error:FMLInferenceModel failed to create D3D12 resource"));
		return nullptr;
	}

	if (Resource && DebugName)
	{
		Resource->SetName(DebugName);
	}

	return Resource;
}

//
//
//
int FMLInferenceModelDml::PrepareTensorShapesAndData()
{
	for (FTensorDesc SymbolicTensorDesc : AllSymbolicTensorDescs)
	{
		if (!SymbolicTensorDesc.GetShape().IsConcrete())
		{
			UE_LOG(LogNNX, Warning, TEXT("DML engine does not support model with variable shapes yet."));
			return -1;
		}
	}

	return 0;
}

//
//
//
static TUniquePtr<FMLRuntimeDml> FDmlRuntimeCreate(bool bRegisterOnlyOperators)
{
	auto Runtime = MakeUnique<FMLRuntimeDml>();

	if (!Runtime->Init(bRegisterOnlyOperators))
	{
		UE_LOG(LogNNX, Warning, TEXT("Failed to create NNX DML runtime"));
		Runtime.Release();
	}

	return Runtime;
}

//
// Called on RDG runtime startup
//
IRuntime* FMLRuntimeDmlStartup()
{
	if (!GDmlRuntime)
	{
		bool bIsD3D12RHI = GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12;		
		bool bLoadDirectML = true;

		if (IsRunningCommandlet() && !IsAllowCommandletRendering())
		{
			UE_LOG(LogNNX, Display, TEXT("Running inside commandlet without rendering"));
			bLoadDirectML = false;
		}

#ifdef DIRECTML_BIN_PATH
		
		if (bIsD3D12RHI && bLoadDirectML)
		{
			const FString DirectMLRuntimeBinPath = TEXT(PREPROCESSOR_TO_STRING(DIRECTML_BIN_PATH));
			FString DirectMLDLLPaths[2];
			int32	NumPaths = 1;
		
			DirectMLDLLPaths[0] = DirectMLRuntimeBinPath / TEXT("DirectML.dll");

			if (GetID3D12PlatformDynamicRHI()->IsD3DDebugEnabled())
			{
				DirectMLDLLPaths[1] = DirectMLRuntimeBinPath / TEXT("DirectML.Debug.dll");
				++NumPaths;
			}

			FPlatformProcess::PushDllDirectory(*DirectMLRuntimeBinPath);

			for (int32 Idx = 0; Idx < NumPaths; ++Idx)
			{
				if (!FPaths::FileExists(DirectMLDLLPaths[Idx]))
				{
					const FString ErrorMessage = FString::Format(TEXT("DirectML DLL file not found in \"{0}\"."),
						{ IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*DirectMLDLLPaths[Idx])});
					UE_LOG(LogNNX, Warning, TEXT("NNXRuntimeDll:%s"), *ErrorMessage);
					checkf(false, TEXT("%s"), *ErrorMessage);
				}

				FPlatformProcess::GetDllHandle(*DirectMLDLLPaths[Idx]);
			}

			FPlatformProcess::PopDllDirectory(*DirectMLRuntimeBinPath);
		}
#endif

		const bool bRegisterOnlyOperators = !bLoadDirectML;

		GDmlRuntime = FDmlRuntimeCreate(bRegisterOnlyOperators);
	}

	return GDmlRuntime.Get();
}

//
// Called on RDG runtime shutdown
//
void FMLRuntimeDmlShutdown()
{
	if (GDmlRuntime)
	{
		GDmlRuntime.Release();
	}
}

} // namespace NNX
 
#endif
