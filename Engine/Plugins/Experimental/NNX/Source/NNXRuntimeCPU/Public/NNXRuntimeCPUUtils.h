// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/UnrealMemory.h"
#include "NNXTypes.h"
#include "NNXRuntime.h"
#include "NNXThirdPartyWarningDisabler.h"
NNX_THIRD_PARTY_INCLUDES_START
#undef check
#undef TEXT
#include "core/session/onnxruntime_cxx_api.h"
NNX_THIRD_PARTY_INCLUDES_END

DECLARE_STATS_GROUP(TEXT("MachineLearning"), STATGROUP_MachineLearning, STATCAT_Advanced);

using TypeInfoORT = std::pair<EMLTensorDataType, uint64>;

inline TypeInfoORT TranslateTensorTypeORTToNNI(unsigned int OrtDataType) 
{
	EMLTensorDataType DataType = EMLTensorDataType::None;
	uint64 ElementSize = 0;

	switch (OrtDataType) {
	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UNDEFINED:
		DataType = EMLTensorDataType::None;
		ElementSize = 0;
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
		DataType = EMLTensorDataType::Float;
		ElementSize = sizeof(float);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
		DataType = EMLTensorDataType::UInt8;
		ElementSize = sizeof(uint8);
		break;
	case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
		DataType = EMLTensorDataType::Int8;
		ElementSize = sizeof(int8);
		break;
	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:
		DataType = EMLTensorDataType::UInt16;
		ElementSize = sizeof(uint16);
		break;
	case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:
		DataType = EMLTensorDataType::Int16;
		ElementSize = sizeof(int16);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
		DataType = EMLTensorDataType::Int32;
		ElementSize = sizeof(int32);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
		DataType = EMLTensorDataType::Int64;
		ElementSize = sizeof(int64);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING:
		DataType = EMLTensorDataType::Char;
		ElementSize = sizeof(char);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
		DataType = EMLTensorDataType::Boolean;
		ElementSize = sizeof(bool);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
		DataType = EMLTensorDataType::Half;
		ElementSize = 2;
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:
		DataType = EMLTensorDataType::Double;
		ElementSize = sizeof(double);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:
		DataType = EMLTensorDataType::UInt32;
		ElementSize = sizeof(uint32);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:
		DataType = EMLTensorDataType::UInt64;
		ElementSize = sizeof(uint64);
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64:
		DataType = EMLTensorDataType::Complex64;
		ElementSize = 8;
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128:
		DataType = EMLTensorDataType::Complex128;
		ElementSize = 16;
		break;

	case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:
		DataType = EMLTensorDataType::BFloat16;
		ElementSize = 2;
		break;

	default:
		DataType = EMLTensorDataType::None;
		break;
	}

	return TypeInfoORT{DataType, ElementSize};
}

inline void BindTensorsToORT(
	TConstArrayView<const NNX::FMLTensorBinding> InBindingTensors,
	TConstArrayView<NNX::FTensor> InTensors,
	TConstArrayView<ONNXTensorElementDataType> InTensorsORTType, 
	const Ort::MemoryInfo* InAllocatorInfo,
	TArray<Ort::Value>& OutOrtTensors
)
{
	const uint32 NumBinding = InBindingTensors.Num();
	const uint32 NumDescriptors = InTensors.Num();

	if (NumBinding != NumDescriptors)
	{
		UE_LOG(LogNNX, Error, TEXT("BindTensorsToORT: Number of Bindings is different of Descriptors."));
		return;
	}

	for (uint32 Index = 0; Index < NumBinding; ++Index)
	{
		const NNX::FMLTensorBinding& Binding = InBindingTensors[Index];
		const NNX::FTensor& Tensor = InTensors[Index];
		const ONNXTensorElementDataType CurrentORTType = InTensorsORTType[Index];

		TUniquePtr<int64_t[]> SizesInt64t;
		SizesInt64t = MakeUnique<int64_t[]>(Tensor.GetShape().Rank());
		for (int32 DimIndex = 0; DimIndex < Tensor.GetShape().Rank(); ++DimIndex)
		{
			SizesInt64t.Get()[DimIndex] = Tensor.GetShape().Data[DimIndex];
		}

		const uint64 ByteCount { InTensors[Index].GetDataSize() };
		const uint32 ArrayDimensions { (uint32)Tensor.GetShape().Rank() };
		OutOrtTensors.Emplace(
			Ort::Value::CreateTensor(
				*InAllocatorInfo,
				Binding.CpuMemory,
				ByteCount, 
				SizesInt64t.Get(),
				ArrayDimensions, 
				CurrentORTType
			)
		);
	}
}

inline void CopyFromORTToBindings(
	TConstArrayView<Ort::Value> InOrtTensors,
	TConstArrayView<const NNX::FMLTensorBinding> InBindingTensors,
	TConstArrayView<NNX::FTensorDesc> InTensorDescs,
	TArray<NNX::FTensor>& OutTensors
)
{
	const uint32 NumBinding = InOrtTensors.Num();
	const uint32 NumDescriptors = InTensorDescs.Num();

	if (NumBinding != NumDescriptors)
	{
		UE_LOG(LogNNX, Error, TEXT("CopyFromORTToBindings: Number of Bindings is different of Descriptors."));
		return;
	}

	for (uint32 Index = 0; Index < NumBinding; ++Index)
	{
		const NNX::FMLTensorBinding& Binding = InBindingTensors[Index];
		const NNX::FTensorDesc& TensorDesc = InTensorDescs[Index];
		const Ort::Value& OrtTensor = InOrtTensors[Index];
		const std::vector<int64_t>& OrtShape = OrtTensor.GetTensorTypeAndShapeInfo().GetShape();

		NNX::FTensorShape Shape;
		for (int32 DimIndex = 0; DimIndex < OrtShape.size(); ++DimIndex)
		{
			check(OrtShape[DimIndex] >= 0);
			Shape.Data.Add(OrtShape[DimIndex]);
		}

		NNX::FTensor Tensor = NNX::FTensor::Make(TensorDesc.GetName(), Shape, TensorDesc.GetDataType());
		OutTensors.Emplace(Tensor);

		void* CpuMemory = (uint8_t*)Binding.CpuMemory + Binding.OffsetInBytes;
		const void* OrtTensorData = OrtTensor.GetTensorData<void>();
		const uint64 DataSize = FMath::Min(Tensor.GetDataSize(), Binding.SizeInBytes);

		if (DataSize > 0)
		{
			FMemory::Memcpy(CpuMemory, OrtTensorData, DataSize);
		}

		if (Tensor.GetDataSize() > Binding.SizeInBytes)
		{
			UE_LOG(LogNNX, Error, TEXT("CopyFromORTToBindings: Binding buffer was not large enough to contain all of the data, only first %d bytes were copied."), DataSize);
		}
	}
}