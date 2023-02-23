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

static constexpr uint32_t NcdhwDimensionCount = 5;
static constexpr uint32_t NcdhwSpatialDimensionCount = 3;
static constexpr uint32_t NonspatialDimensionCount = 2; // The batch and channel dimensions of NCW, NCHW, NCDHW....

template<typename T>
inline TArrayView<T> MakeEmptyArrayView()
{
	return MakeArrayView(static_cast<T*>(nullptr), 0);
}

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
	template<typename T>
	using FSmallArray = TArray<T, TInlineAllocator<NNECore::FTensorShape::MaxRank>>;
	using FSmallIntArray = TArray<int32, TInlineAllocator<NNECore::FTensorShape::MaxRank>>;
	using FSmallUIntArray = TArray<uint32, TInlineAllocator<NNECore::FTensorShape::MaxRank>>;

	struct FTensorDesc
	{
		DML_BUFFER_TENSOR_DESC	BuffDesc;
		DML_TENSOR_DESC			Desc;
		// Don't edit Sizes and Strides directly, use methods
		FSmallUIntArray			Sizes;
		FSmallUIntArray			Strides;
		uint64					ElemSizeInBytes;

		bool InitFromTensor(const NNECore::Internal::FTensor& InputDesc, int32 MinTensorRank, TConstArrayView<uint32> Broadcast = MakeArrayView((uint32*) nullptr, 0), TConstArrayView<uint32> CustomShape = MakeArrayView((uint32*) nullptr, 0));
		bool InitFromTensor1D(const NNECore::Internal::FTensor& InputDesc, int32 Rank);

		void SetStridesFromTensor(const NNECore::Internal::FTensor& InputDesc);

		void UpdateShapeAndStrides(TConstArrayView<uint32> InShape, TConstArrayView<uint32> InStrides = MakeArrayView((uint32*) nullptr, 0));

	private:
		void Reset();

		void SetShape(TConstArrayView<uint32> Shape, int32 MinTensorRank);
		void SetShapeAndStrides(TConstArrayView<uint32> Shape, TConstArrayView<uint32> BroadcastShape);
		void SetShape1D(uint32 Dimension, int32 Rank);
		
		void Update(DML_TENSOR_DATA_TYPE DataType, bool bHasWeightData = false);
		uint64 CalculateBufferSize();
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

	virtual TConstArrayView<int32> GetConstantCPUInputs() const;

	IDMLOperator* GetOperator();

protected:

	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const NNECore::Internal::FTensor& Tensor);
	bool InitDmlTensorDesc(DmlUtil::FTensorDesc& DmlTensorDesc, const NNECore::Internal::FTensor& Tensor, const NNECore::Internal::FTensor& Broadcast);
	
	bool CreateOperator(IDMLDevice* Device, const DML_OPERATOR_DESC& DmlOpDesc);

	TComPtr<IDMLOperator>		DmlOp;
	DmlUtil::FSmallIntArray		ConstantCPUInputs;
};

/**
 * DirectML ML operator registry
 */
using FOperatorRegistryDml = TOperatorRegistryRDG<FOperatorDml>;


} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
