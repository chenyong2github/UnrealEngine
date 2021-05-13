// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemGpuComputeProxy.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraComputeExecutionContext.h"
#include "NiagaraGPUSystemTick.h"

FNiagaraSystemGpuComputeProxy::FNiagaraSystemGpuComputeProxy(FNiagaraSystemInstance* OwnerInstance)
	: DebugOwnerInstance(OwnerInstance)
	, DebugOwnerBatcher(nullptr)
	, SystemInstanceID(OwnerInstance->GetId())
{
	bRequiresDistanceFieldData = OwnerInstance->RequiresDistanceFieldData();
	bRequiresDepthBuffer = OwnerInstance->RequiresDepthBuffer();
	bRequiresEarlyViewData = OwnerInstance->RequiresEarlyViewData();
	bRequiresViewUniformBuffer = OwnerInstance->RequiresViewUniformBuffer();

	// Gather all emitter compute contexts
	for ( auto& Emitter : OwnerInstance->GetEmitters() )
	{
		if (FNiagaraComputeExecutionContext* ComputeContext = Emitter->GetGPUContext() )
		{
			ComputeContexts.Emplace(ComputeContext);
		}
	}

	// Calculate Tick Stage
	if (bRequiresDistanceFieldData || bRequiresDepthBuffer)
	{
		ComputeTickStage = ENiagaraGpuComputeTickStage::PostOpaqueRender;
	}
	else if (bRequiresEarlyViewData)
	{
		ComputeTickStage = ENiagaraGpuComputeTickStage::PostInitViews;
	}
	else if (bRequiresViewUniformBuffer)
	{
		ComputeTickStage = ENiagaraGpuComputeTickStage::PostOpaqueRender;
	}
	else
	{
		ComputeTickStage = ENiagaraGpuComputeTickStage::PreInitViews;
	}
}

FNiagaraSystemGpuComputeProxy::~FNiagaraSystemGpuComputeProxy()
{
	check(IsInRenderingThread());
	check(DebugOwnerBatcher == nullptr);
}

void FNiagaraSystemGpuComputeProxy::AddToBatcher(NiagaraEmitterInstanceBatcher* Batcher)
{
	check(IsInGameThread());
	check(DebugOwnerBatcher == nullptr);
	DebugOwnerBatcher = Batcher;

	ENQUEUE_RENDER_COMMAND(AddProxyToBatcher)(
		[=](FRHICommandListImmediate& RHICmdList)
		{
			Batcher->AddGpuComputeProxy(this);

			for (FNiagaraComputeExecutionContext* ComputeContext : ComputeContexts)
			{
				ComputeContext->bHasTickedThisFrame_RT = false;
				ComputeContext->CurrentNumInstances_RT = 0;
				ComputeContext->CurrentMaxInstances_RT = 0;

				for (int i=0; i < UE_ARRAY_COUNT(ComputeContext->DataBuffers_RT); ++i)
				{
					check(ComputeContext->DataBuffers_RT[i] == nullptr);
					ComputeContext->DataBuffers_RT[i] = new FNiagaraDataBuffer(ComputeContext->MainDataSet);
				}
			}
		}
	);
}

void FNiagaraSystemGpuComputeProxy::RemoveFromBatcher(NiagaraEmitterInstanceBatcher* Batcher, bool bDeleteProxy)
{
	check(IsInGameThread());
	check(DebugOwnerBatcher == Batcher);
	DebugOwnerBatcher = nullptr;

	ENQUEUE_RENDER_COMMAND(RemoveFromBatcher)(
		[=](FRHICommandListImmediate& RHICmdList)
		{
			Batcher->RemoveGpuComputeProxy(this);
			ReleaseTicks(Batcher->GetGPUInstanceCounterManager());

			for (FNiagaraComputeExecutionContext* ComputeContext : ComputeContexts)
			{
				ComputeContext->ResetInternal(Batcher);

				//-TODO: Can we move this inside the context???
				for (int i = 0; i < UE_ARRAY_COUNT(ComputeContext->DataBuffers_RT); ++i)
				{
					check(ComputeContext->DataBuffers_RT[i]);
					ComputeContext->DataBuffers_RT[i]->ReleaseGPU();
					ComputeContext->DataBuffers_RT[i]->Destroy();
					ComputeContext->DataBuffers_RT[i] = nullptr;
				}
			}

			if (bDeleteProxy)
			{
				delete this;
			}
		}
	);
}

void FNiagaraSystemGpuComputeProxy::QueueTick(const FNiagaraGPUSystemTick& Tick)
{
	check(IsInRenderingThread());

	//if ( !FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()) )
	//{
	//	return;
	//}

	//-OPT: Making a copy of the tick here, reduce this.
	PendingTicks.Add(Tick);

	// Consume DataInterface instance data
	//-TODO: This should be consumed as the command in executed rather than here, otherwise ticks are not processed correctly
	//       However an audit of all data interfaces is required to ensure they pass data safely, for example the skeletal mesh one does not
	if (Tick.DIInstanceData)
	{
		uint8* BasePointer = (uint8*)Tick.DIInstanceData->PerInstanceDataForRT;

		for (auto& Pair : Tick.DIInstanceData->InterfaceProxiesToOffsets)
		{
			FNiagaraDataInterfaceProxy* Proxy = Pair.Key;
			uint8* InstanceDataPtr = BasePointer + Pair.Value;
			Proxy->ConsumePerInstanceDataFromGameThread(InstanceDataPtr, Tick.SystemInstanceID);
		}
	}
}

void FNiagaraSystemGpuComputeProxy::ReleaseTicks(FNiagaraGPUInstanceCountManager& GPUInstanceCountManager)
{
	check(IsInRenderingThread());

	// Release all the ticks
	for (auto& Tick : PendingTicks)
	{
		Tick.Destroy();
	}
	PendingTicks.Empty();

	for (FNiagaraComputeExecutionContext* ComputeContext : ComputeContexts)
	{
		// Reset pending information as this will have the readback incorporated into it
		ComputeContext->bHasTickedThisFrame_RT = false;
		ComputeContext->CurrentMaxInstances_RT = 0;

		// Clear counter offsets
		for (int i = 0; i < UE_ARRAY_COUNT(ComputeContext->DataBuffers_RT); ++i)
		{
			ComputeContext->DataBuffers_RT[i]->ClearGPUInstanceCount();
		}
	}
}
