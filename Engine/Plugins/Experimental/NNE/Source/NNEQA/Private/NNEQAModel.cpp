// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEQAModel.h"
#include "NNECoreModelData.h"
#include "RenderGraphResources.h"
#include "ShaderParameterUtils.h"

BEGIN_SHADER_PARAMETER_STRUCT(FNNEQATensorReadbackParameters, )
	RDG_BUFFER_ACCESS(Buffer, ERHIAccess::CopySrc)
END_SHADER_PARAMETER_STRUCT()

namespace UE::NNEQA::Private 
{
namespace FModelQAHelpers
{
	FRDGBufferDesc CreateRDGBufferDescForTensorDesc(uint32 ElemByteSize, uint64 SizeInByte)
	{
		// FIXME: CreateStructuredDesc() creates a crash on VulkanRHI
		//FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(ElemByteSize, SizeInByte/ElemByteSize);
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(ElemByteSize, SizeInByte/ElemByteSize);

		// FIXME: We should use BUF_SourceCopy for only output buffers (GPU readback)
		Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy);

		return Desc;
	}

	void ConvertBinding(FRDGBuilder& GraphBuilder,
		TConstArrayView<NNECore::FTensorDesc> InTensorDescs,
		TConstArrayView<NNECore::FTensorBindingCPU> InBindingsCPU,
		TArray<NNECore::FTensorBindingRDG>& OutBindingsRDG)
	{
		check(IsInRenderingThread());
		check(InTensorDescs.Num() == InBindingsCPU.Num());

		for (int32 Idx = 0; Idx < InBindingsCPU.Num(); ++Idx)
		{
			const NNECore::FTensorDesc& TensorDesc = InTensorDescs[Idx];
			const NNECore::FTensorBindingCPU& BindingCPU = InBindingsCPU[Idx];
			const FRDGBufferDesc Desc = CreateRDGBufferDescForTensorDesc(TensorDesc.GetElemByteSize(), BindingCPU.SizeInBytes);
			FRDGBufferRef TensorBuffer = GraphBuilder.CreateBuffer(Desc, *TensorDesc.GetName(), ERDGBufferFlags::None);

			NNECore::FTensorBindingRDG& BindingRDG = OutBindingsRDG.Emplace_GetRef();
			BindingRDG.Buffer = TensorBuffer;
		}
	}

	void UploadsBindingToGPU(FRDGBuilder& GraphBuilder,
		TConstArrayView<NNECore::FTensorBindingCPU> InBindingsCPU,
		TConstArrayView<NNECore::FTensorBindingRDG> InBindingsRDG)
	{
		check(IsInRenderingThread());
		check(InBindingsCPU.Num() == InBindingsRDG.Num());

		for (int32 Idx = 0; Idx < InBindingsCPU.Num(); ++Idx)
		{
			const NNECore::FTensorBindingCPU& BindingCPU = InBindingsCPU[Idx];
			const NNECore::FTensorBindingRDG& BindingRDG = InBindingsRDG[Idx];
			GraphBuilder.QueueBufferUpload(BindingRDG.Buffer, BindingCPU.Data, BindingCPU.SizeInBytes, ERDGInitialDataFlags::NoCopy);
		}
	}

	void DownloadBindingToCPU(FRDGBuilder& GraphBuilder,
		TArray<FModelQA::FReadbackEntry>& Readbacks,
		TConstArrayView<NNECore::FTensorBindingRDG> InBindingsRDG,
		TConstArrayView<NNECore::FTensorBindingCPU> InBindingsCPU)
	{
		check(IsInRenderingThread());
		check(InBindingsCPU.Num() == InBindingsRDG.Num());

		for (int32 Idx = 0; Idx < InBindingsRDG.Num(); ++Idx)
		{
			const NNECore::FTensorBindingCPU& BindingCPU = InBindingsCPU[Idx];
			const NNECore::FTensorBindingRDG& BindingRDG = InBindingsRDG[Idx];
			
			FNNEQATensorReadbackParameters* TensorReadbackParams = GraphBuilder.AllocParameters<FNNEQATensorReadbackParameters>();

			TensorReadbackParams->Buffer = BindingRDG.Buffer;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("FNNEQAModelTensorReadback"),
				TensorReadbackParams,
				ERDGPassFlags::Readback | ERDGPassFlags::NeverCull,
				[&Readbacks, BindingCPU, TensorReadbackParams](FRHICommandListImmediate& RHICmdList)
				{
					FRHIBuffer* OutputBuffer = TensorReadbackParams->Buffer->GetRHI();

					// TODO: FIXME: We need to transition the resources for DirectML
					/*if (bUseManualTransitions)
					{
						FRHITransitionInfo Transitions[] =
						{
							FRHITransitionInfo(OutputBuffer, ERHIAccess::UAVCompute, ERHIAccess::CopySrc)
						};

						RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));
						RHICmdList.SubmitCommandsHint();
					}*/

					FModelQA::FReadbackEntry& Readback = Readbacks.Emplace_GetRef();
					Readback.RHI = MakeUnique<FRHIGPUBufferReadback>(FName(TEXT("FNNEQAModelTensorReadback")));
					Readback.RHI->EnqueueCopy(RHICmdList, OutputBuffer, BindingCPU.SizeInBytes);
					Readback.CpuMemory = BindingCPU.Data;
					Readback.Size = BindingCPU.SizeInBytes;
				}
			);
		}
	}
}
	
TConstArrayView<NNECore::FTensorDesc> FModelQA::GetInputTensorDescs() const
{ 
	if (ModelCPU.IsValid()) 
		return ModelCPU->GetInputTensorDescs();
	if (ModelRDG.IsValid())
		return ModelRDG->GetInputTensorDescs();
	else
		return {};
};
		
TConstArrayView<NNECore::FTensorDesc> FModelQA::GetOutputTensorDescs() const
{
	if (ModelCPU.IsValid())
		return ModelCPU->GetOutputTensorDescs();
	if (ModelRDG.IsValid())
		return ModelRDG->GetOutputTensorDescs();
	else
		return {};
}

TConstArrayView<NNECore::FTensorShape> FModelQA::GetInputTensorShapes() const
{
	if (ModelCPU.IsValid())
		return ModelCPU->GetInputTensorShapes();
	if (ModelRDG.IsValid())
		return ModelRDG->GetInputTensorShapes();
	else
		return {};
}

TConstArrayView<NNECore::FTensorShape> FModelQA::GetOutputTensorShapes() const
{
	if (ModelCPU.IsValid())
		return ModelCPU->GetOutputTensorShapes();
	if (ModelRDG.IsValid())
		return ModelRDG->GetOutputTensorShapes();
	else
		return {};
}

int FModelQA::SetInputTensorShapes(TConstArrayView<NNECore::FTensorShape> InInputShapes)
{
	if (ModelCPU.IsValid())
		return ModelCPU->SetInputTensorShapes(InInputShapes);
	if (ModelRDG.IsValid())
		return ModelRDG->SetInputTensorShapes(InInputShapes);
	else
		return {};
}
	
int FModelQA::EnqueueRDG(FRDGBuilder& RDGBuilder,
	TConstArrayView<NNECore::FTensorBindingCPU> InInputBindings, 
	TConstArrayView<NNECore::FTensorBindingCPU> InOutputBindings)
{
	check(IsInRenderingThread());
			
	if (!ModelRDG.IsValid())
	{
		return -1;
	}
			
	TArray<NNECore::FTensorBindingRDG> InputBindingsRDG;
	TArray<NNECore::FTensorBindingRDG> OutputBindingsRDG;
			
	FModelQAHelpers::ConvertBinding(RDGBuilder, ModelRDG->GetInputTensorDescs(), InInputBindings, InputBindingsRDG);
	FModelQAHelpers::ConvertBinding(RDGBuilder, ModelRDG->GetOutputTensorDescs(), InOutputBindings, OutputBindingsRDG);
			
	FModelQAHelpers::UploadsBindingToGPU(RDGBuilder, InInputBindings, InputBindingsRDG);

	int32 Res = ModelRDG->EnqueueRDG(RDGBuilder, InputBindingsRDG, OutputBindingsRDG);

	FModelQAHelpers::DownloadBindingToCPU(RDGBuilder, Readbacks, OutputBindingsRDG, InOutputBindings);
			
	return Res;
}

int FModelQA::RunSync(TConstArrayView<NNECore::FTensorBindingCPU> InInputBindings, TConstArrayView<NNECore::FTensorBindingCPU> InOutputBindings)
{
	if (ModelCPU.IsValid())
	{
		return ModelCPU->RunSync(InInputBindings, InOutputBindings);
	}
	else if (ModelRDG.IsValid())
	{
		// TODO ok if pending?
		Readbacks.Empty();

		int Res = 0;
		FEvent* Signal = FGenericPlatformProcess::GetSynchEventFromPool(false);

		ENQUEUE_RENDER_COMMAND(FModelQA_Run)
		(
			[&Signal, &Res, this, InInputBindings, InOutputBindings](FRHICommandListImmediate& RHICmdList)
			{
				TOptional<ERHIPipeline> Pipeline = RHICmdList.GetPipeline();

				if (Pipeline == ERHIPipeline::None)
				{
					RHICmdList.SwitchPipeline(ERHIPipeline::Graphics);
				}

				FRDGBuilder	RDGBuilder(RHICmdList);

				Res = EnqueueRDG(RDGBuilder, InInputBindings, InOutputBindings);
				if (Res == 0)
				{
					RDGBuilder.Execute();

					// FIXME: Using BlockUntilGPUIdle() prevents hang on Linux
					// FIXME: Adapt to redesigned readback API (UE 5.2)
					RHICmdList.BlockUntilGPUIdle();

					// Process readback
					for (const FReadbackEntry& Readback : Readbacks) {
						const void* BuffData = Readback.RHI->Lock(Readback.Size);
						check(BuffData);
						FMemory::Memcpy(Readback.CpuMemory, BuffData, Readback.Size);
						Readback.RHI->Unlock();
					}
				}

				Signal->Trigger();
			}
		);

		// We need to wait for render thread to finish
		Signal->Wait();
				
		FGenericPlatformProcess::ReturnSynchEventToPool(Signal);

		return Res;
	}
	else
	{
		return -1;
	}
}

TUniquePtr<FModelQA> FModelQA::MakeModelQA(const FNNIModelRaw& ONNXModelData, const FString& RuntimeName)
{
	TUniquePtr<FModelQA> ModelQA = MakeUnique<FModelQA>();
	TWeakInterfacePtr<INNERuntime> Runtime = NNECore::GetRuntime<INNERuntime>(RuntimeName);
	TWeakInterfacePtr<INNERuntimeCPU> RuntimeCPU = NNECore::GetRuntime<INNERuntimeCPU>(RuntimeName);
	TWeakInterfacePtr<INNERuntimeRDG> RuntimeRDG = NNECore::GetRuntime<INNERuntimeRDG>(RuntimeName);
	TObjectPtr<UNNEModelData> ModelData = NewObject<UNNEModelData>();
			
	ModelData->Init(FString("onnx"), ONNXModelData.Data);
	if (!Runtime.IsValid())
	{
		UE_LOG(LogNNE, Error, TEXT("Can't get %s runtime."), *RuntimeName);
		return TUniquePtr<FModelQA>();
	}
	if (RuntimeCPU.IsValid())
	{
		ModelQA->ModelCPU = RuntimeCPU->CreateModelCPU(ModelData);
	}
	else if (RuntimeRDG.IsValid())
	{
		ModelQA->ModelRDG = RuntimeRDG->CreateModelRDG(ModelData);
	}
	else
	{
		UE_LOG(LogNNE, Error, TEXT("Can't find supported API for %s runtime."), *RuntimeName);
		return TUniquePtr<FModelQA>();
	}
	return ModelQA;
}
	
} // namespace UE::NNEQA::Private