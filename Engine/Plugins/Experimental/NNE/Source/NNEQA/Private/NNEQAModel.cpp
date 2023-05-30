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
	FRDGBufferDesc CreateRDGBufferDescForTensorDesc(uint32 ElemByteSize, uint64 SizeInByte, bool bIsInput)
	{
		// FIXME: CreateStructuredDesc() creates a crash on VulkanRHI
		//FRDGBufferDesc Desc = FRDGBufferDesc::CreateStructuredDesc(ElemByteSize, SizeInByte/ElemByteSize);
		FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(ElemByteSize, SizeInByte/ElemByteSize);

		// FIXME: We should use BUF_SourceCopy for only output buffers (GPU readback)
		if (bIsInput)
		{
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_Static);
		}
		else
		{
			Desc.Usage = EBufferUsageFlags(Desc.Usage | BUF_SourceCopy | BUF_UnorderedAccess);
		}

		return Desc;
	}

	void ConvertBinding(TConstArrayView<NNECore::FTensorBindingCPU> InBindingsCPU, TArray<NNECore::FTensorBindingGPU>& OutBindingsGPU)
	{
		for (int32 Idx = 0; Idx < InBindingsCPU.Num(); ++Idx)
		{
			const NNECore::FTensorBindingCPU& BindingCPU = InBindingsCPU[Idx];
			NNECore::FTensorBindingGPU& BindingGPU = OutBindingsGPU.Emplace_GetRef();
			BindingGPU.Data = BindingCPU.Data;
			BindingGPU.SizeInBytes = BindingCPU.SizeInBytes;
		}
	}

	void ConvertBinding(FRDGBuilder& GraphBuilder,
		TConstArrayView<NNECore::FTensorDesc> InTensorDescs,
		TConstArrayView<NNECore::FTensorBindingCPU> InBindingsCPU,
		TArray<NNECore::FTensorBindingRDG>& OutBindingsRDG,
		bool bIsInput)
	{
		check(IsInRenderingThread());
		check(InTensorDescs.Num() == InBindingsCPU.Num());

		for (int32 Idx = 0; Idx < InBindingsCPU.Num(); ++Idx)
		{
			const NNECore::FTensorDesc& TensorDesc = InTensorDescs[Idx];
			const NNECore::FTensorBindingCPU& BindingCPU = InBindingsCPU[Idx];
			const FRDGBufferDesc Desc = CreateRDGBufferDescForTensorDesc(TensorDesc.GetElemByteSize(), BindingCPU.SizeInBytes, bIsInput);
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

					//Notes: We need to transition the resources for DirectML
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
	if (ModelInstanceCPU.IsValid()) 
		return ModelInstanceCPU->GetInputTensorDescs();
	if (ModelInstanceGPU.IsValid())
		return ModelInstanceGPU->GetInputTensorDescs();
	if (ModelInstanceRDG.IsValid())
		return ModelInstanceRDG->GetInputTensorDescs();
	else
		return {};
};
		
TConstArrayView<NNECore::FTensorDesc> FModelQA::GetOutputTensorDescs() const
{
	if (ModelInstanceCPU.IsValid())
		return ModelInstanceCPU->GetOutputTensorDescs();
	if (ModelInstanceGPU.IsValid())
		return ModelInstanceGPU->GetOutputTensorDescs();
	if (ModelInstanceRDG.IsValid())
		return ModelInstanceRDG->GetOutputTensorDescs();
	else
		return {};
}

TConstArrayView<NNECore::FTensorShape> FModelQA::GetInputTensorShapes() const
{
	if (ModelInstanceCPU.IsValid())
		return ModelInstanceCPU->GetInputTensorShapes();
	if (ModelInstanceGPU.IsValid())
		return ModelInstanceGPU->GetInputTensorShapes();
	if (ModelInstanceRDG.IsValid())
		return ModelInstanceRDG->GetInputTensorShapes();
	else
		return {};
}

TConstArrayView<NNECore::FTensorShape> FModelQA::GetOutputTensorShapes() const
{
	if (ModelInstanceCPU.IsValid())
		return ModelInstanceCPU->GetOutputTensorShapes();
	if (ModelInstanceGPU.IsValid())
		return ModelInstanceGPU->GetOutputTensorShapes();
	if (ModelInstanceRDG.IsValid())
		return ModelInstanceRDG->GetOutputTensorShapes();
	else
		return {};
}

int FModelQA::SetInputTensorShapes(TConstArrayView<NNECore::FTensorShape> InInputShapes)
{
	if (ModelInstanceCPU.IsValid())
		return ModelInstanceCPU->SetInputTensorShapes(InInputShapes);
	if (ModelInstanceGPU.IsValid())
		return ModelInstanceGPU->SetInputTensorShapes(InInputShapes);
	if (ModelInstanceRDG.IsValid())
		return ModelInstanceRDG->SetInputTensorShapes(InInputShapes);
	else
		return {};
}
	
int FModelQA::EnqueueRDG(FRDGBuilder& RDGBuilder,
	TConstArrayView<NNECore::FTensorBindingCPU> InInputBindings, 
	TConstArrayView<NNECore::FTensorBindingCPU> InOutputBindings)
{
	check(IsInRenderingThread());
			
	if (!ModelInstanceRDG.IsValid())
	{
		return -1;
	}
			
	TArray<NNECore::FTensorBindingRDG> InputBindingsRDG;
	TArray<NNECore::FTensorBindingRDG> OutputBindingsRDG;
			
	FModelQAHelpers::ConvertBinding(RDGBuilder, ModelInstanceRDG->GetInputTensorDescs(), InInputBindings, InputBindingsRDG, true);
	FModelQAHelpers::ConvertBinding(RDGBuilder, ModelInstanceRDG->GetOutputTensorDescs(), InOutputBindings, OutputBindingsRDG, false);
			
	FModelQAHelpers::UploadsBindingToGPU(RDGBuilder, InInputBindings, InputBindingsRDG);

	int32 Res = ModelInstanceRDG->EnqueueRDG(RDGBuilder, InputBindingsRDG, OutputBindingsRDG);

	FModelQAHelpers::DownloadBindingToCPU(RDGBuilder, Readbacks, OutputBindingsRDG, InOutputBindings);
			
	return Res;
}

int FModelQA::RunSync(TConstArrayView<NNECore::FTensorBindingCPU> InInputBindings, TConstArrayView<NNECore::FTensorBindingCPU> InOutputBindings)
{
	if (ModelInstanceCPU.IsValid())
	{
		return ModelInstanceCPU->RunSync(InInputBindings, InOutputBindings);
	}
	if (ModelInstanceGPU.IsValid())
	{
		TArray<NNECore::FTensorBindingGPU> InputBindingsGPU;
		TArray<NNECore::FTensorBindingGPU> OutputBindingsGPU;
		FModelQAHelpers::ConvertBinding(InInputBindings, InputBindingsGPU);
		FModelQAHelpers::ConvertBinding(InOutputBindings, OutputBindingsGPU);
		return ModelInstanceGPU->RunSync(InputBindingsGPU, OutputBindingsGPU);
	}
	else if (ModelInstanceRDG.IsValid())
	{
		//Note: ok if pending?
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

TUniquePtr<FModelQA> FModelQA::MakeModelQA(const FNNEModelRaw& ONNXModelData, const FString& RuntimeName)
{
	TUniquePtr<FModelQA> ModelQA = MakeUnique<FModelQA>();
	TWeakInterfacePtr<INNERuntime> Runtime = NNECore::GetRuntime<INNERuntime>(RuntimeName);
	TWeakInterfacePtr<INNERuntimeCPU> RuntimeCPU = NNECore::GetRuntime<INNERuntimeCPU>(RuntimeName);
	TWeakInterfacePtr<INNERuntimeGPU> RuntimeGPU = NNECore::GetRuntime<INNERuntimeGPU>(RuntimeName);
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
		ModelQA->ModelInstanceCPU = RuntimeCPU->CreateModel(ModelData)->CreateModelInstance();
	}
	else if (RuntimeGPU.IsValid())
	{
		ModelQA->ModelInstanceGPU = RuntimeGPU->CreateModel(ModelData)->CreateModelInstance();
	}
	else if (RuntimeRDG.IsValid())
	{
		ModelQA->ModelInstanceRDG = RuntimeRDG->CreateModel(ModelData)->CreateModelInstance();
	}
	else
	{
		UE_LOG(LogNNE, Error, TEXT("Can't find supported API for %s runtime."), *RuntimeName);
		return TUniquePtr<FModelQA>();
	}
	return ModelQA;
}
	
} // namespace UE::NNEQA::Private