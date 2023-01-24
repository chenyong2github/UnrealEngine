// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGModel.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"
#include "NNEUtilsModelOptimizer.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RHIGPUReadback.h"

#include "Serialization/MemoryReader.h"

namespace UE::NNERuntimeRDG::Private
{

bool FModelRDG::LoadModel(TConstArrayView<uint8> ModelData, FMLRuntimeFormat& Format, int32 GuidAndVersionSize)
{
	TConstArrayView<uint8> ModelBuffer = { &(ModelData.GetData()[GuidAndVersionSize]), ModelData.Num() - GuidAndVersionSize };

	FMemoryReaderView Reader(ModelBuffer);

	FMLRuntimeFormat::StaticStruct()->SerializeBin(Reader, &Format);

	// Data for base class
	InputSymbolicTensors.Empty();
	OutputSymbolicTensors.Empty();

	// Data for RDG
	AllSymbolicTensorDescs.Empty();
	IntermediateTensorIndices.Empty();
	WeightTensorIndices.Empty();
	InputTensorIndices.Empty();
	OutputTensorIndices.Empty();
	OperatorInputTensorIndices.Empty();
	OperatorOutputTensorIndices.Empty();

	// Add tensors
	for (int32 Idx = 0; Idx < Format.Tensors.Num(); ++Idx)
	{
		const FMLFormatTensorDesc& FormatTensorDesc = Format.Tensors[Idx];

		const NNECore::FSymbolicTensorShape SymbolicShape = NNECore::FSymbolicTensorShape::Make(FormatTensorDesc.Shape);
		const NNECore::FTensorDesc SymbolicTensor = NNECore::FTensorDesc::Make(FormatTensorDesc.Name, SymbolicShape, FormatTensorDesc.DataType);

		AllSymbolicTensorDescs.Emplace(SymbolicTensor);

		if (FormatTensorDesc.Type == EMLFormatTensorType::Input)
		{
			InputTensorIndices.Emplace(Idx);
			InputSymbolicTensors.Emplace(SymbolicTensor);
		}
		else if (FormatTensorDesc.Type == EMLFormatTensorType::Output)
		{
			OutputTensorIndices.Emplace(Idx);
			OutputSymbolicTensors.Emplace(SymbolicTensor);
		}
		else if (FormatTensorDesc.Type == EMLFormatTensorType::Intermediate)
		{
			IntermediateTensorIndices.Emplace(Idx);
		}
		else if (FormatTensorDesc.Type == EMLFormatTensorType::Initializer)
		{
			WeightTensorIndices.Emplace(Idx);
			if (!SymbolicTensor.GetShape().IsConcrete())
			{
				UE_LOG(LogNNE, Error, TEXT("Weight tensor %s should have a concrete shape"), *SymbolicTensor.GetName());
				return false;
			}

			const NNECore::FTensorShape TensorShape = NNECore::FTensorShape::MakeFromSymbolic(SymbolicTensor.GetShape());
			FTensorRDG& WeightRDG = WeightTensorRDGs.Emplace_GetRef(FTensorRDG::Make(SymbolicTensor, TensorShape, nullptr));

			if (WeightRDG.GetDataSize() != FormatTensorDesc.DataSize)
			{
				UE_LOG(LogNNE, Error, TEXT("Weight %s has incorrect size. Expected %d bytes, got %d"), *SymbolicTensor.GetName(), FormatTensorDesc.DataSize, WeightRDG.GetDataSize());
				return false;
			}

			const uint8* DataPtr = Format.TensorData.GetData() + FormatTensorDesc.DataOffset;
			TConstArrayView<uint8> DataView = MakeArrayView(DataPtr, FormatTensorDesc.DataSize);

			WeightRDG.SetPreparedData(DataView);
		}
		checkf(FormatTensorDesc.Type != EMLFormatTensorType::None, TEXT("Unsupported tensor type None"));
	}

	// Loop over all operators in the model and store tensor indices for input/output
	for (int32 Idx = 0; Idx < Format.Operators.Num(); ++Idx)
	{
		OperatorInputTensorIndices.Emplace(Format.Operators[Idx].InTensors);
		OperatorOutputTensorIndices.Emplace(Format.Operators[Idx].OutTensors);
	}

	return true;
}



int FModelRDG::SetInputTensorShapes(TConstArrayView<FTensorShape> InInputShapes)
{
	OutputTensorShapes.Empty();

	//Verify input shape are valid for the model and set InputTensorShapes
	if (FModelBase<NNECore::IModelRDG>::SetInputTensorShapes(InInputShapes) != 0)
	{
		return -1;
	}

	//Allocate and prime all AllTensorRDGs with concrete shapes defaulting variables dimension to 1 if needed
	AllTensorRDGs.Init(nullptr, AllSymbolicTensorDescs.Num());

	InputTensorRDGs.Empty();
	for (int32 i = 0; i < InputTensorIndices.Num(); ++i)
	{
		const int32 Idx = InputTensorIndices[i];
		const FTensorDesc& TensorDesc = InputSymbolicTensors[i];
		const FTensorShape& TensorShape = InputTensorShapes[i];

		InputTensorRDGs.Emplace(FTensorRDG::Make(TensorDesc, TensorShape, nullptr));
		AllTensorRDGs[Idx] = &InputTensorRDGs[i];
	}

	for (int32 i = 0; i < WeightTensorIndices.Num(); ++i)
	{
		const int32 Idx = WeightTensorIndices[i];

		AllTensorRDGs[Idx] = &WeightTensorRDGs[i];
	}

	IntermediateTensorRDGs.Empty();
	for (int32 i = 0; i < IntermediateTensorIndices.Num(); ++i)
	{
		const int32 Idx = IntermediateTensorIndices[i];
		const FTensorDesc& TensorDesc = AllSymbolicTensorDescs[Idx];
		const FTensorShape TensorShape = FTensorShape::MakeFromSymbolic(TensorDesc.GetShape());

		IntermediateTensorRDGs.Emplace(FTensorRDG::Make(TensorDesc, TensorShape, nullptr));
		AllTensorRDGs[Idx] = &IntermediateTensorRDGs[i];
	}

	OutputTensorRDGs.Empty();
	for (int32 i = 0; i < OutputTensorIndices.Num(); ++i)
	{
		const int32 Idx = OutputTensorIndices[i];
		const FTensorDesc& TensorDesc = OutputSymbolicTensors[i];
		const FTensorShape TensorShape = FTensorShape::MakeFromSymbolic(TensorDesc.GetShape());

		OutputTensorRDGs.Emplace(FTensorRDG::Make(TensorDesc, TensorShape, nullptr));
		AllTensorRDGs[Idx] = &OutputTensorRDGs[i];
	}

	checkCode(
		for (int i = 0; i < AllTensorRDGs.Num(); ++i)
		{
			checkf(AllTensorRDGs[i] != nullptr, TEXT("Tensor at index %d, was not allocated for model preparation."), i);
		};
	);

	//Allow the specific engine to run shape inference if supported
	if (PrepareTensorShapesAndData() != 0)
	{
		return -1;
	}

	checkCode(
		for (int i = 0; i < AllTensorRDGs.Num(); ++i)
		{
			checkf(AllTensorRDGs[i] != nullptr, TEXT("Tensor at index %d, was not allocated after model preparation."), i);
			checkf(AllTensorRDGs[i]->GetShape().IsCompatibleWith(AllSymbolicTensorDescs[i].GetShape()), TEXT("Tensor at index %d have a shape incompatible with model definition."), i);
		};
	);

	//Set OutputTensorShapes for the model from preparation result
	for (int32 OutputIndices : OutputTensorIndices)
	{
		OutputTensorShapes.Emplace(AllTensorRDGs[OutputIndices]->GetShape());
	}

	check(InputTensorIndices.Num() + OutputTensorIndices.Num() + WeightTensorIndices.Num() + IntermediateTensorIndices.Num() == AllTensorRDGs.Num());
	check(InputTensorShapes.Num() == InputSymbolicTensors.Num());
	check(OutputTensorShapes.Num() == OutputSymbolicTensors.Num());
	check(WeightTensorIndices.Num() == WeightTensorRDGs.Num());
	check(AllTensorRDGs.Num() == AllSymbolicTensorDescs.Num());

	return 0;
}

FRDGBufferDesc CreateRDGBufferDescForTensorRDG(const FTensorRDG& Tensor)
{
	// FIXME: CreateStructuredDesc() creates a crash on VulkanRHI
	//FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(Tensor.GetElemByteSize(), Tensor.GetVolume());
	FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(Tensor.GetElemByteSize(), Tensor.GetVolume());

	return Desc;
}

/**
 * Enqueue operators to RDG, the caller will run the GraphBuilder.Execute()
 */
int FModelRDG::EnqueueRDG(FRDGBuilder& RDGBuilder, TConstArrayView<NNECore::FTensorBindingRDG> InInputBindings, TConstArrayView<NNECore::FTensorBindingRDG> InOutputBindings)
{
	check(IsInRenderingThread());

	int Res;

	// Verify the model inputs were prepared
	if (InputTensorShapes.Num() == 0)
	{
		UE_LOG(LogNNE, Error, TEXT("EnqueueRDG(): Input shapes are not set, please call SetInputTensorShapes."));
		return -1;
	}

	Res = SetTensors(RDGBuilder, InputTensorRDGs, InInputBindings);
	if (Res != 0)
	{
		UE_LOG(LogNNE, Warning, TEXT("Invalid input tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	Res = SetTensors(RDGBuilder, OutputTensorRDGs, InOutputBindings);
	if (Res != 0)
	{
		UE_LOG(LogNNE, Warning, TEXT("Invalid output tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	//Create buffer for intermediate tensors
	for (FTensorRDG& TensorRDG : IntermediateTensorRDGs)
	{
		const FRDGBufferDesc BufferDesc = CreateRDGBufferDescForTensorRDG(TensorRDG);
		const FRDGBufferRef TensorBuffer = RDGBuilder.CreateBuffer(BufferDesc, *TensorRDG.GetName(), ERDGBufferFlags::None);
		check(TensorRDG.GetBuffer() == nullptr);
		TensorRDG.SetBuffer(TensorBuffer);
	}

	// TODO: FIXME: DirectML uses RHI buffers instead of RDG buffers
	//For now weights tensors are not uploaded to GPU thus GetBuffer will return nullptr for them.
	//checkCode(for (const FTensorRDG* TensorRDG : AllTensorRDGs) { if (TensorRDG != nullptr) { check(TensorRDG->GetBuffer() != nullptr); } });

	//Insert weights tensors
	for (int32 i = 0; i < WeightTensorIndices.Num(); ++i)
	{
		AllTensorRDGs[WeightTensorIndices[i]] = &WeightTensorRDGs[i];
	}

	// We can now dispatch operators
	AddDispatchOps_RenderThread(RDGBuilder);

	return 0;
}

int FModelRDG::SetTensors(FRDGBuilder& GraphBuilder, FTensorRDGArray& InTensorRDGs, TConstArrayView<NNECore::FTensorBindingRDG> InBindings)
{
	check(InBindings.Num() == InTensorRDGs.Num());

	for (int32 Idx = 0; Idx < InBindings.Num(); ++Idx)
	{
		FTensorRDG& TensorRDG = InTensorRDGs[Idx];
		const NNECore::FTensorBindingRDG& Binding = InBindings[Idx];
		TensorRDG.SetBuffer(Binding.Buffer);
	}

	return 0;
}

}