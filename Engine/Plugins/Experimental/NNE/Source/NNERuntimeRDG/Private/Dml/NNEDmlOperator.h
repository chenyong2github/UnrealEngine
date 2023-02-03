// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifdef NNE_USE_DIRECTML

#include "NNEDmlCommon.h"
#include "NNECoreTypes.h"
#include "NNECoreTensor.h"
#include "NNECoreOperator.h"
#include "NNECoreAttributeMap.h"
#include "NNERuntimeRDG.h"

#define NNE_DML_REGISTER_OP(OpName) \
struct FDmlOperator##OpName##Registrator \
{ \
	FDmlOperator##OpName##Registrator() \
	{ \
		FOperatorRegistryDml::Get()->OpAdd(TEXT(#OpName), FOperatorDml##OpName##::Create); \
	} \
}; \
\
static FDmlOperator##OpName##Registrator RegisterDmlOperator##OpName;


namespace UE::NNERuntimeRDG::Private::Dml
{
//
//
//
class FDmlDeviceContext
{
public:

	uint32							DeviceIndex;
	ID3D12Device*					D3D12Device{ nullptr }; // Borrowed reference from RHI
	TComPtr<IDMLDevice>				Device{ nullptr };
	TComPtr<IDMLCommandRecorder>	CmdRec{ nullptr };
};

namespace DmlUtil
{
	struct FTensorDesc
	{
		DML_BUFFER_TENSOR_DESC												BuffDesc;
		DML_TENSOR_DESC														Desc;
		TArray<uint32, TInlineAllocator<NNECore::FTensorShape::MaxRank>>	Sizes;
		TArray<uint32, TInlineAllocator<NNECore::FTensorShape::MaxRank>>	Strides;
	};

	extern void SetTensorStrides(FTensorDesc& TensorDesc, const NNECore::Internal::FTensor& InputDesc);
	extern void SetTensorSizesAndStridesForBroadcast(FTensorDesc& TensorDesc, const NNECore::Internal::FTensor& InputDesc, const NNECore::Internal::FTensor& TargetDesc);
	extern bool IsSameShape(const NNECore::Internal::FTensor& Left, const NNECore::Internal::FTensor& Right);
	DML_TENSOR_DATA_TYPE GetTensorDataType(ENNETensorDataType DataType);
	uint64 CalculateBufferSize(const FTensorDesc& DmlTensor, const NNECore::Internal::FTensor& Desc);
}

//
// DirectML operator base class
//
class FOperatorDml
{
public:

	virtual ~FOperatorDml() = default;

	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) = 0;

	IDMLOperator* GetOperator();

protected:

	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const NNECore::Internal::FTensor& TensorDesc);
	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const NNECore::Internal::FTensor& TensorDesc, const NNECore::Internal::FTensor& BroadcastDesc);
	
	bool CreateOperator(IDMLDevice* Device, const DML_OPERATOR_DESC& DmlOpDesc);

	TComPtr<IDMLOperator>		DmlOp;
};

/**
 * DirectML ML operator registry
 */
using FOperatorRegistryDml = TOperatorRegistryRDG<FOperatorDml>;


} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
