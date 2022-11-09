// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntime.h"
#include "NNXCore.h"

namespace NNX
{

	FMLTensorBinding FMLTensorBinding::FromCPU(void* CpuMemory, uint64 InSize, uint64 InOffset)
	{
		FMLTensorBinding	Binding;

		Binding.BindingType = EMLTensorBindingDataType::CPUMemory;
		Binding.CpuMemory = CpuMemory;
		Binding.SizeInBytes = InSize;
		Binding.OffsetInBytes = InOffset;

		return Binding;
	}

	FMLTensorBinding FMLTensorBinding::FromGPU(uint64 GpuMemory, uint64 InSize, uint64 InOffset)
	{
		FMLTensorBinding	Binding;

		Binding.BindingType = EMLTensorBindingDataType::GPUMemory;
		Binding.GpuMemory = GpuMemory;
		Binding.SizeInBytes = InSize;
		Binding.OffsetInBytes = InOffset;

		return Binding;
	}

	FMLTensorBinding FMLTensorBinding::FromRDG(FRDGBuffer* BufferRef, uint64 InSize, uint64 InOffset)
	{
		FMLTensorBinding	Binding;

		Binding.BindingType = EMLTensorBindingDataType::RDGBuffer;
		Binding.Buffer = BufferRef;
		Binding.SizeInBytes = InSize;
		Binding.OffsetInBytes = InOffset;

		return Binding;
	}

	int FMLInferenceModel::SetInputShapes(TConstArrayView<FTensorShape> InInputShapes)
	{
		if (InInputShapes.Num() != InputSymbolicTensors.Num())
		{
			UE_LOG(LogNNX, Warning, TEXT("Number of input shapes does not match number of input tensors"));
			return -1;
		}

		for (int32 i = 0; i < InInputShapes.Num(); ++i)
		{
			const FTensorDesc SymbolicDesc = InputSymbolicTensors[i];
			if (!InInputShapes[i].IsCompatibleWith(SymbolicDesc.GetShape()))
			{
				UE_LOG(LogNNX, Warning, TEXT("Input shape does not match input tensor %s of index %d"), *SymbolicDesc.GetName(), i);
				return -1;
			}
		}

		//Implementations are responsible to handle shape inference.
		//This base implementation only validate that all inputs are
		//matching what the model can support.
		return 0;
	}

}