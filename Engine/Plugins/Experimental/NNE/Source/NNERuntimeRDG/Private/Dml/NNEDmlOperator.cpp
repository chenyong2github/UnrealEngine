// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NNE_USE_DIRECTML

#include "NNEDmlOperator.h"
#include "ID3D12DynamicRHI.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

namespace DmlUtil
{
	void SetTensorStrides(FTensorDesc& TensorDesc, const NNECore::Internal::FTensor& InputDesc)
	{
		uint32 CurrStride = 1;

		TensorDesc.Strides.SetNum(InputDesc.GetShape().Rank());
		
		for (int32 i = InputDesc.GetShape().Rank() - 1; i >= 0; --i)
		{
			TensorDesc.Strides[i] = CurrStride;
			CurrStride *= InputDesc.GetShape().GetData()[i];
		}
	}

	void SetTensorSizesAndStridesForBroadcast(FTensorDesc& TensorDesc, const NNECore::Internal::FTensor& InputDesc, const NNECore::Internal::FTensor& TargetDesc)
	{
		static_assert(NNECore::FTensorShape::MaxRank <= 8);
		
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

	bool IsSameShape(const NNECore::Internal::FTensor& Left, const NNECore::Internal::FTensor& Right)
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

	uint64 CalculateBufferSize(const FTensorDesc& DmlTensor, const NNECore::Internal::FTensor& Desc)
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
// DirectML operator base class
//
IDMLOperator* FOperatorDml::GetOperator()
{
	return DmlOp;
}

bool FOperatorDml::InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const NNECore::Internal::FTensor& TensorDesc)
{
	DML_TENSOR_DATA_TYPE DmlDataType = DmlUtil::GetTensorDataType(TensorDesc.GetDataType());

	if (DmlDataType == DML_TENSOR_DATA_TYPE_UNKNOWN)
	{
		DmlTensorDesc.BuffDesc = DML_BUFFER_TENSOR_DESC{};
		DmlTensorDesc.Desc = DML_TENSOR_DESC{};

		return false;
	}

	DmlTensorDesc.Sizes = TensorDesc.GetShape().GetData();
	//Note: We should support tensor padding using strides defined in FTensorDesc
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

bool FOperatorDml::InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const NNECore::Internal::FTensor& TensorDesc, const NNECore::Internal::FTensor& BroadcastDesc)
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

	//UE_LOG(LogNNE, Warning, TEXT("DmlTensorDesc:%d,%d,%d -> %d,%d,%d"),
	//	TensorDesc.Sizes[0],
	//	TensorDesc.Dimension > 1 ? TensorDesc.Sizes[1] : 0,
	//	TensorDesc.Dimension > 2 ? TensorDesc.Sizes[2] : 0,
	//	DmlTensorDesc.Sizes[0],
	//	DmlTensorDesc.Dimension > 1 ? DmlTensorDesc.Sizes[1] : 0,
	//	DmlTensorDesc.Dimension > 2 ? DmlTensorDesc.Sizes[2] : 0
	//);

	//UE_LOG(LogNNE, Warning, TEXT("DmlTensorStrides:%d,%d,%d"),
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

// Create DirectML operator	
bool FOperatorDml::CreateOperator(IDMLDevice* Device, const DML_OPERATOR_DESC& DmlOpDesc)
{
	IDMLOperator* Op = nullptr;

	HRESULT Res;

	Res = Device->CreateOperator(&DmlOpDesc, DML_PPV_ARGS(&Op));
	if (!Op)
	{
		UE_LOG(LogNNE, Warning, TEXT("Error:Failed to create DML operator, hres:%d"), Res);
		return false;
	}

	DmlOp = Op;

	return DmlOp.IsValid();
}

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
