// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXRuntimeRDG.h"
#include "NNXInferenceModel.h"
#include "NNXRuntimeFormat.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

#include "Serialization/MemoryReader.h"

namespace NNX
{

//
//
//
FMLInferenceModelRDG::FMLInferenceModelRDG()
	: FMLInferenceModel(EMLInferenceModelType::RDG)
{
}

//
//
//
bool FMLInferenceModelRDG::LoadModel(UMLInferenceModel* InModel, FMLRuntimeFormat& Format)
{
	EMLInferenceFormat FormatType = InModel->GetFormat();

	if (FormatType != EMLInferenceFormat::NNXRT)
	{
		UE_LOG(LogNNX, Warning, TEXT("Unsupported format type for NNX inference model"));
		return false;
	}

	FMemoryReader Reader(InModel->GetData());

	FMLRuntimeFormat::StaticStruct()->SerializeBin(Reader, &Format);

	// Add tensors
	for (int32 Idx = 0; Idx < Format.Tensors.Num(); ++Idx)
	{
		const FMLFormatTensorDesc& TensorDesc = Format.Tensors[Idx];

		FMLTensorDesc	Tensor;

		Tensor.Name = TensorDesc.Name;

		Tensor.Dimension = TensorDesc.Shape.Dimension;
		if (Tensor.Dimension > FMLTensorDesc::MaxTensorDimension)
		{
			// FIXME: Should we report error here?
			Tensor.Dimension = FMLTensorDesc::MaxTensorDimension;
		}

		FMemory::Memset(Tensor.Sizes, 0, sizeof(Tensor.Sizes));
		for (uint32 ShapeIdx = 0; ShapeIdx < Tensor.Dimension; ++ShapeIdx)
		{
			Tensor.Sizes[ShapeIdx] = TensorDesc.Shape.Sizes[ShapeIdx];
		}

		// NOTE: Set first the DataType prior setting DataSize
		Tensor.DataType = TensorDesc.DataType;
		Tensor.DataSize = Tensor.GetElemByteSize() * Tensor.Volume();

		if (TensorDesc.Type == EMLFormatTensorType::Input)
		{
			InputTensors.Add(Tensor);
		}
		else if (Format.Tensors[Idx].Type == EMLFormatTensorType::Output)
		{
			OutputTensors.Add(Tensor);
		}
		// FIXME: Read intermediate tensors
	}

	return true;
}

/**
 * Run the inference model (synchronous version)
 */
int FMLInferenceModelRDG::Run(TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> OutOutputBindings)
{
	FEvent* Signal = FGenericPlatformProcess::GetSynchEventFromPool(false);
	int		Res = 0;

	ENQUEUE_RENDER_COMMAND(FMLInferenceModel_Run)
	(
		[&Signal, &Res, this, InInputBindings, OutOutputBindings](FRHICommandListImmediate& RHICmdList)
		{
			TOptional<ERHIPipeline>		Pipeline = RHICmdList.GetPipeline();

			if (Pipeline == ERHIPipeline::None)
			{
				RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
			}

			FRDGBuilder	GraphBuilder(RHICmdList);

			Res = EnqueueRDG(GraphBuilder, InInputBindings, OutOutputBindings);
			if (Res == 0)
			{
				GraphBuilder.Execute();
			}
			
			Signal->Trigger();
		}
	);

	// We need to wait for render thread to finish
	Signal->Wait();

	return Res;
}

/**
 * Enqueue operators to RDG, the caller will run the GraphBuilder.Execute()
 */
int FMLInferenceModelRDG::EnqueueRDG(FRDGBuilder& GraphBuilder, TArrayView<const FMLTensorBinding> InInputBindings, TArrayView<const FMLTensorBinding> InOutputBindings)
{
	check(IsInRenderingThread());

	int Res;

	// Process input tensors, and if required, allocate RDG buffers
	FMLTensorBindingArray	RDGInputBindings;
	FMLIntArray				RDGUploadIndices;

	Res = SetTensors(GraphBuilder, RDGInputBindings, RDGUploadIndices, InInputBindings, InputTensors);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid input tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	// Process output tensors, and if required, allocate RDG buffers
	FMLTensorBindingArray	RDGOutputBindings;
	FMLIntArray				RDGReadbackIndices;

	Res = SetTensors(GraphBuilder, RDGOutputBindings, RDGReadbackIndices, InOutputBindings, OutputTensors);
	if (Res != 0)
	{
		UE_LOG(LogNNX, Warning, TEXT("Invalid output tensor binding type for tensor index:%d"), Res);
		return -1;
	}

	// If required, upload input tensors to GPU
	if (!RDGUploadIndices.IsEmpty())
	{
		AddTensorUploads_RenderThread(GraphBuilder, RDGUploadIndices, RDGInputBindings, InInputBindings);
	}

	// We can now dispatch operators
	AddDispatchOps_RenderThread(GraphBuilder, RDGInputBindings, RDGOutputBindings);

	// If required, readback the output tensors to CPU
	if (!RDGReadbackIndices.IsEmpty())
	{
		AddTensorReadbacks_RenderThread(GraphBuilder, RDGReadbackIndices, RDGOutputBindings, InOutputBindings);
	}

	return 0;
}

/** 
 * Process tensor bindings and check if we need to create RDG Buffer for CPU binding 
 * Returns 0 on success, or index of a tensor binding if the tensor binding type is not supported.
 */
int FMLInferenceModelRDG::SetTensors(FRDGBuilder& GraphBuilder, FMLTensorBindingArray& OutBindings, FMLIntArray& OutIndices, TArrayView<const FMLTensorBinding> InBindings, TArrayView<const FMLTensorDesc> InTensors)
{
	for (int32 Idx = 0; Idx < InBindings.Num(); ++Idx)
	{
		const FMLTensorBinding& Binding = InBindings[Idx];
		const FMLTensorDesc& TensorDesc = InTensors[Idx];

		if (Binding.BindingType == EMLTensorBindingDataType::CPUMemory)
		{
			FRDGBufferRef	TensorBuffer;

			TensorBuffer = GraphBuilder.CreateBuffer(
				FRDGBufferDesc::CreateBufferDesc(TensorDesc.GetElemByteSize(), TensorDesc.Num()),
				*TensorDesc.Name,
				ERDGBufferFlags::None);

			OutBindings.Emplace(FMLTensorBinding::FromRDG(TensorBuffer, InTensors[Idx].DataSize));

			OutIndices.Add(Idx);
		}
		else if (Binding.BindingType == EMLTensorBindingDataType::RDGBuffer)
		{
			OutBindings.Add(Binding);
		}
		else
		{
			// Unsupported tensor binding type
			return Idx;
		}
	}

	return 0;
}

//
//
//
void FMLInferenceModelRDG::AddTensorUploads_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const int32> InUploadIndices, TArrayView<FMLTensorBinding> InRDGBindings, TArrayView<const FMLTensorBinding> InBindings)
{
	for (int32 Idx = 0; Idx < InUploadIndices.Num(); ++Idx)
	{
		const int32				TensorIdx = InUploadIndices[Idx];
		const FMLTensorBinding& RDGBinding = InRDGBindings[TensorIdx];
		const FMLTensorBinding& InBinding = InBindings[TensorIdx];
		const FMLTensorDesc&	TensorDesc = InputTensors[TensorIdx];

		FMLTensorUploadParameters* TensorUploadParams = GraphBuilder.AllocParameters<FMLTensorUploadParameters>();

		TensorUploadParams->Buffer = RDGBinding.Buffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLInferenceModelAddTensorUpload"),
			TensorUploadParams,
			ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
			[InBinding, TensorDesc, TensorUploadParams](FRHICommandListImmediate& RHICmdList)
			{
				void* BuffData = RHICmdList.LockBuffer(TensorUploadParams->Buffer->GetRHI(), 0, TensorDesc.DataSize, RLM_WriteOnly);
				FMemory::Memcpy(BuffData, InBinding.CpuMemory, TensorDesc.DataSize);
				RHICmdList.UnlockBuffer(TensorUploadParams->Buffer->GetRHI());
			}
		);
	}
}

//
//
//
void FMLInferenceModelRDG::AddTensorReadbacks_RenderThread(FRDGBuilder& GraphBuilder, TArrayView<const int32> InReadbackIndices, TArrayView<const FMLTensorBinding> InRDGBindings, TArrayView<const FMLTensorBinding> InBindings)
{	
	for (int32 Idx = 0; Idx < InReadbackIndices.Num(); ++Idx)
	{
		const int32				TensorIdx = InReadbackIndices[Idx];
		const FMLTensorBinding& RDGBinding = InRDGBindings[TensorIdx];
		const FMLTensorBinding& InBinding = InBindings[TensorIdx];
		const FMLTensorDesc&	TensorDesc = OutputTensors[TensorIdx];

		FMLTensorReadbackParameters* TensorReadbackParams = GraphBuilder.AllocParameters<FMLTensorReadbackParameters>();

		TensorReadbackParams->Buffer = RDGBinding.Buffer;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("FMLInferenceModelAddTensorReadback"),
			TensorReadbackParams,
			ERDGPassFlags::Readback | ERDGPassFlags::NeverCull,
			[InBinding, TensorDesc, TensorReadbackParams](FRHICommandListImmediate& RHICmdList)
			{
				FRHIBuffer* OutputBuffer = TensorReadbackParams->Buffer->GetRHI();

				FRHITransitionInfo PreTransitions[] =
				{
					FRHITransitionInfo(OutputBuffer, ERHIAccess::UAVCompute, ERHIAccess::CopySrc)
				};

				RHICmdList.Transition(MakeArrayView(PreTransitions, UE_ARRAY_COUNT(PreTransitions)));

				// TODO: FIXME: We need to transition the resources for DirectML
				RHICmdList.SubmitCommandsHint();

				FRHIGPUBufferReadback	Readback("FMLTensorReadback");

				Readback.EnqueueCopy(RHICmdList, OutputBuffer, TensorDesc.DataSize);
				RHICmdList.BlockUntilGPUIdle();
				check(Readback.IsReady());

				const void* BuffData = Readback.Lock(TensorDesc.DataSize);
				FMemory::Memcpy(InBinding.CpuMemory, BuffData, TensorDesc.DataSize);
				Readback.Unlock();

				//const void* BuffData = RHICmdList.LockBuffer(OutputBuffer, 0, TensorDesc.DataSize, RLM_ReadOnly);
				//FMemory::Memcpy(InBinding.CpuMemory, BuffData, TensorDesc.DataSize);
				//RHICmdList.UnlockBuffer(OutputBuffer);

				//FRHITransitionInfo PostTransitions[] =
				//{
				//	FRHITransitionInfo(OutputBuffer, ERHIAccess::CopySrc, ERHIAccess::UAVCompute)
				//};

				//RHICmdList.Transition(MakeArrayView(PostTransitions, UE_ARRAY_COUNT(PostTransitions)));
			}
		);
	}
}


//
//
//
//void FMLInferenceModelRDG::AddTensorUpload_RenderThread(FRDGBuilder& GraphBuilder, FRDGBufferRef TensorBuffer, const FMLTensorBinding& InTensorBinding, const FMLTensorDesc& TensorDesc)
//{
//	FMLTensorUploadParameters* TensorUploadParams = GraphBuilder.AllocParameters<FMLTensorUploadParameters>();
//
//	TensorUploadParams->Buffer = TensorBuffer;
//
//	GraphBuilder.AddPass(
//		RDG_EVENT_NAME("NNXDmlTensorUpload"),
//		TensorUploadParams,
//		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
//		[InTensorBinding, TensorDesc, TensorUploadParams](FRHICommandListImmediate& RHICmdList)
//		{
//			// Copy input					
//			void* BuffData = RHICmdList.LockBuffer(TensorUploadParams->Buffer->GetRHI(), 0, TensorDesc.DataSize, RLM_WriteOnly);
//			FMemory::Memcpy(BuffData, InTensorBinding.CpuMemory, TensorDesc.DataSize);
//			RHICmdList.UnlockBuffer(TensorUploadParams->Buffer->GetRHI());
//		}
//	);
//}

//
//
//
//void FMLInferenceModelRDG::AddTensorReadback_RenderThread(FRDGBuilder& GraphBuilder, const FMLTensorBinding& InTensorBinding, const FMLTensorDesc& TensorDesc)
//{	
//	FMLTensorReadbackParameters* TensorReadbackParams = GraphBuilder.AllocParameters<FMLTensorReadbackParameters>();
//
//	TensorReadbackParams->Buffer = TensorBuffer;
//
//	GraphBuilder.AddPass(
//		RDG_EVENT_NAME("NNXDmlTensorReadback"),
//		TensorReadbackParams,
//		ERDGPassFlags::Copy | ERDGPassFlags::NeverCull,
//		[InTensorBinding, TensorDesc, TensorReadbackParams](FRHICommandListImmediate& RHICmdList)
//		{
//			// Copy input					
//			void* BuffData = RHICmdList.LockBuffer(TensorReadbackParams->Buffer->GetRHI(), 0, TensorDesc.DataSize, RLM_WriteOnly);
//			FMemory::Memcpy(BuffData, InTensorBinding.CpuMemory, TensorDesc.DataSize);
//			RHICmdList.UnlockBuffer(TensorReadbackParams->Buffer->GetRHI());
//		}
//	);
//}

} // NNX
