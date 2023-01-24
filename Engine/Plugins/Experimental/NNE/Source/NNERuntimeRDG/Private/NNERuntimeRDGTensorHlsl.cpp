// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGTensorHlsl.h"
#include "RenderGraphBuilder.h"

namespace UE::NNERuntimeRDG::Private::Hlsl
{

void FTensorHLSL::EnqueueUploadRdg(FRDGBuilder& GraphBuilder)
{
	if (!HasPreparedData() && !HasUploadBuffer()) return;
	
	check(HasPreparedData() != HasUploadBuffer());
	check(HasBuffer());

	FCpuBufferRef CPUBuffer = HasUploadBuffer() ? GetUploadBuffer() : PreparedData.GetData();
	GraphBuilder.QueueBufferUpload(GetBuffer(), CPUBuffer, GetDataSize(), ERDGInitialDataFlags::NoCopy);
}

void FTensorHLSL::EnqueueDownloadRdg(FRDGBuilder& GraphBuilder, bool bUseManualTransitions)
{
	if (!HasDownloadBuffer()) return;

	check(HasBuffer());
	check(!Readback);

	Readback = MakeUnique<FRHIGPUBufferReadback>(FName(TEXT("FMLTensorReadback_") + GetName()));

	FNNETensorReadbackParameters* TensorReadbackParams = GraphBuilder.AllocParameters<FNNETensorReadbackParameters>();
	TensorReadbackParams->Buffer = GetBuffer();

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("FMLInferenceModelAddTensorReadback:%s", *GetName()),
		TensorReadbackParams,
		ERDGPassFlags::Readback | ERDGPassFlags::NeverCull,
		[this, bUseManualTransitions, TensorReadbackParams](FRHICommandListImmediate& RHICmdList)
		{
			FRHIBuffer* OutputBuffer = TensorReadbackParams->Buffer->GetRHI();

			// TODO: FIXME: We need to transition the resources for DirectML
			if (bUseManualTransitions)
			{
				FRHITransitionInfo Transitions[] =
				{
					FRHITransitionInfo(OutputBuffer, ERHIAccess::UAVCompute, ERHIAccess::CopySrc)
				};

				RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));
				RHICmdList.SubmitCommandsHint();
			}

			Readback->EnqueueCopy(RHICmdList, OutputBuffer, GetDataSize());
		}
	);
}

void FTensorHLSL::Resolve()
{
	if (!Readback) return;

	const void* BuffData = Readback->Lock(GetDataSize());
	check(BuffData);

	FMemory::Memcpy(GetDownloadBuffer(), BuffData, GetDataSize());

	Readback->Unlock();
}

} // namespace UE::NNERuntimeRDG::Private::Hlsl