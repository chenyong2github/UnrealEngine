// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraScriptExecutionContext.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "NiagaraStats.h"
#include "NiagaraShader.h"
#include "NiagaraSortingGPU.h"
#include "NiagaraWorldManager.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "Runtime/Engine/Private/GPUSort.h"

DECLARE_CYCLE_STAT(TEXT("Niagara Dispatch Setup"), STAT_NiagaraGPUDispatchSetup_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Emitter Dispatch [RT]"), STAT_NiagaraGPUSimTick_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Data Readback [RT]"), STAT_NiagaraGPUReadback_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Allocate GPU Readback Data [RT]"), STAT_NiagaraAllocateGPUReadback_RT, STATGROUP_Niagara);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Niagara GPU Sim"), STAT_GPU_NiagaraSim, STATGROUP_GPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Particles"), STAT_NiagaraGPUParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Particles"), STAT_NiagaraGPUSortedParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Buffers"), STAT_NiagaraGPUSortedBuffers, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Readback latency (frames)"), STAT_NiagaraReadbackLatency, STATGROUP_Niagara);

DECLARE_GPU_STAT_NAMED(NiagaraGPU, TEXT("Niagara"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSimulation, TEXT("Niagara GPU Simulation"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSorting, TEXT("Niagara GPU sorting"));
DECLARE_GPU_STAT_NAMED(NiagaraSimReadback, TEXT("Niagara GPU Simulation read back"));
DECLARE_GPU_STAT_NAMED(NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));

uint32 FNiagaraComputeExecutionContext::TickCounter = 0;

int32 GNiagaraOverlapCompute = 1;
static FAutoConsoleVariableRef CVarNiagaraUseAsyncCompute(
	TEXT("fx.NiagaraOverlapCompute"),
	GNiagaraOverlapCompute,
	TEXT("If 1, use compute dispatch overlap for better performance. \n"),
	ECVF_Default
);

// @todo REMOVE THIS HACK
int32 GNiagaraGpuMaxQueuedRenderFrames = 10;
static FAutoConsoleVariableRef CVarNiagaraGpuMaxQueuedRenderFrames(
	TEXT("fx.NiagaraGpuMaxQueuedRenderFrames"),
	GNiagaraOverlapCompute,
	TEXT("Number of frames we all to pass before we start to discard GPU ticks.\n"),
	ECVF_Default
);

FNiagaraIndicesVertexBuffer::FNiagaraIndicesVertexBuffer(int32 InIndexCount)
	: IndexCount(InIndexCount)
{
	FRHIResourceCreateInfo CreateInfo;
	VertexBufferRHI = RHICreateVertexBuffer((uint32)IndexCount * sizeof(int32), BUF_Static | BUF_ShaderResource | BUF_UnorderedAccess, CreateInfo);
	VertexBufferSRV = RHICreateShaderResourceView(VertexBufferRHI, sizeof(int32), PF_R32_SINT);
	VertexBufferUAV = RHICreateUnorderedAccessView(VertexBufferRHI, PF_R32_SINT);
}

const FName NiagaraEmitterInstanceBatcher::Name(TEXT("NiagaraEmitterInstanceBatcher"));

FFXSystemInterface* NiagaraEmitterInstanceBatcher::GetInterface(const FName& InName)
{
	return InName == Name ? this : nullptr;
}

NiagaraEmitterInstanceBatcher::~NiagaraEmitterInstanceBatcher()
{
	ParticleSortBuffers.ReleaseRHI();

	FinishDispatches();
}

void NiagaraEmitterInstanceBatcher::GiveSystemTick_RenderThread(FNiagaraGPUSystemTick& Tick)
{
	check(IsInRenderingThread());

	// @todo REMOVE THIS HACK
	if (GFrameNumberRenderThread > LastFrameThatDrainedData + GNiagaraGpuMaxQueuedRenderFrames)
	{
		Tick.Destroy();
		return;
	}

	// Now we consume DataInterface instance data.
	if (Tick.DIInstanceData)
	{
		uint8* BasePointer = (uint8*) Tick.DIInstanceData->PerInstanceDataForRT;

		//UE_LOG(LogNiagara, Log, TEXT("RT Give DI (dipacket) %p (baseptr) %p"), Tick.DIInstanceData, BasePointer);
		for(auto& Pair : Tick.DIInstanceData->InterfaceProxiesToOffsets)
		{
			FNiagaraDataInterfaceProxy* Proxy = Pair.Key;
			uint8* InstanceDataPtr = BasePointer + Pair.Value;

			//UE_LOG(LogNiagara, Log, TEXT("\tRT DI (proxy) %p (instancebase) %p"), Proxy, InstanceDataPtr);
			Proxy->ConsumePerInstanceDataFromGameThread(InstanceDataPtr, Tick.SystemInstanceID);
		}
	}

	// A note:
	// This is making a copy of Tick. That structure is small now and we take a copy to avoid
	// making a bunch of small allocations on the game thread. We may need to revisit this.
	Ticks_RT.Add(Tick);
}

void NiagaraEmitterInstanceBatcher::GiveEmitterContextToDestroy_RenderThread(FNiagaraComputeExecutionContext* Context)
{
	ContextsToDestroy_RT.Add(Context);
}

void NiagaraEmitterInstanceBatcher::GiveDataSetToDestroy_RenderThread(FNiagaraDataSet* DataSet)
{
	DataSetsToDestroy_RT.Add(DataSet);
}

void NiagaraEmitterInstanceBatcher::FinishDispatches()
{
	ReleaseTicks();

	for (FNiagaraComputeExecutionContext* Context : ContextsToDestroy_RT)
	{
		delete Context;
	}
	ContextsToDestroy_RT.Reset();

	for (FNiagaraDataSet* DataSet : DataSetsToDestroy_RT)
	{
		delete DataSet;
	}
	DataSetsToDestroy_RT.Reset();

	for (TSharedPtr<FNiagaraDataInterfaceProxy, ESPMode::ThreadSafe>& Proxy : DIProxyDeferredDeletes_RT)
	{
		Proxy->DeferredDestroy();
	}

	DIProxyDeferredDeletes_RT.Empty();
}

void NiagaraEmitterInstanceBatcher::ReleaseTicks()
{
	check(IsInRenderingThread());

	for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		Tick.Destroy();
	}

	Ticks_RT.Empty(0);
}

void NiagaraEmitterInstanceBatcher::ResizeBuffersAndGatherResources(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FNiagaraBufferArray& DestDataBuffers, FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& DestBufferIntFloat, FNiagaraBufferArray& CurrBufferIntFloat)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUDispatchSetup_RT);

	for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
	{
		uint32 DispatchCount = Tick->Count;
		FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
		for (uint32 Index = 0; Index < DispatchCount; Index++)
		{
			FNiagaraComputeInstanceData& Instance = Instances[Index];
			FNiagaraComputeExecutionContext* Context = Instance.Context;
			if ( Context == nullptr )
			{
				continue;
			}

			FNiagaraShader* Shader = Context->GPUScript_RT->GetShader();
			if ( Shader == nullptr )
			{
				continue;
			}

			//The buffer containing current simulation state.
			Instance.CurrentData = Context->MainDataSet->GetCurrentData();
			//The buffer we're going to write simulation results to.
			Instance.DestinationData = &Context->MainDataSet->BeginSimulate();

			check(Instance.CurrentData && Instance.DestinationData);
			FNiagaraDataBuffer& CurrentData = *Instance.CurrentData;
			FNiagaraDataBuffer& DestinationData = *Instance.DestinationData;

			const uint32 PrevNumInstances = CurrentData.GetNumInstances();
			const uint32 NewNumInstances = Instance.SpawnRateInstances + Instance.EventSpawnTotal + PrevNumInstances;

			//We must assume all particles survive when allocating here. 
			//If this is not true, the read back in ResolveDatasetWrites will shrink the buffers.
			const uint32 RequiredInstances = FMath::Max(PrevNumInstances, NewNumInstances);

			DestinationData.AllocateGPU(RequiredInstances + 1, RHICmdList);
			DestinationData.SetNumInstances(RequiredInstances);

			if ( Shader->FloatInputBufferParam.IsBound() )
			{
				CurrDataBuffers.Add(CurrentData.GetGPUBufferFloat().UAV);
			}
			if ( Shader->IntInputBufferParam.IsBound() )
			{
				CurrBufferIntFloat.Add(CurrentData.GetGPUBufferInt().UAV);
			}

			if ( Shader->FloatOutputBufferParam.IsBound() )
			{
				DestDataBuffers.Add(DestinationData.GetGPUBufferFloat().UAV);
			}
			if ( Shader->IntOutputBufferParam.IsBound() )
			{
				DestBufferIntFloat.Add(DestinationData.GetGPUBufferInt().UAV);
			}

			Context->MainDataSet->EndSimulate();
			Context->SetDataToRender(Instance.DestinationData);
		}
	}
}

void NiagaraEmitterInstanceBatcher::DispatchAllOnCompute(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer, FNiagaraBufferArray& DestDataBuffers, FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& DestBufferIntFloat, FNiagaraBufferArray& CurrBufferIntFloat)
{
	FRHICommandListImmediate& RHICmdListImmediate = FRHICommandListExecutor::GetImmediateCommandList();

	// Disable automatic cache flush so that we can have our compute work overlapping. Barrier will be used as a sync mechanism.
	RHICmdList.AutomaticCacheFlushAfterComputeShader(false);

	//
	//	Transition current index buffer ready for compute and clear then all using overlapping compute work items.
	//

	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, CurrDataBuffers.GetData(), CurrDataBuffers.Num());

	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraIndexBufferClear);
		SCOPED_GPU_STAT(RHICmdList, NiagaraIndexBufferClear);

		for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
		{
			uint32 DispatchCount = Tick->Count;
			FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
			for (uint32 Index = 0; Index < DispatchCount; Index++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[Index];
				FNiagaraComputeExecutionContext* Context = Instance.Context;
				if (Context && Context->GPUScript_RT->GetShader())
				{
#if WITH_EDITORONLY_DATA
					if (Context->DebugInfo.IsValid())
					{
						ProcessDebugInfo(RHICmdList, Context);
					}
#endif // WITH_EDITORONLY_DATA

					// clear data set index buffer for the simulation shader to write number of written instances
					ClearUAV(RHICmdList, Instance.DestinationData->GetGPUIndices(), 0);
				}

			}
		}
	}

	//
	//	Add a rw barrier for the destination data buffers we just cleared and mark others as read/write as needed for particles simulation.
	//
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, DestDataBuffers.GetData(), DestDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, DestBufferIntFloat.GetData(), DestBufferIntFloat.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, CurrDataBuffers.GetData(), CurrDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, CurrBufferIntFloat.GetData(), CurrBufferIntFloat.Num());
	RHICmdList.FlushComputeShaderCache();

	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUSimulation);
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSimulation);
		for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
		{
			uint32 DispatchCount = Tick->Count;
			FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
			for (uint32 Index = 0; Index < DispatchCount; Index++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[Index];
				FNiagaraComputeExecutionContext* Context = Instance.Context;
				if (Context && Context->GPUScript_RT->GetShader())
				{
					FNiagaraComputeExecutionContext::TickCounter++;

					// run shader, sim and spawn in a single dispatch
					uint32 UpdateStartInstance = 0;
					Run<false>(*Tick, &Instance, UpdateStartInstance, Instance.DestinationData->GetNumInstances(), Context->GPUScript_RT->GetShader(), RHICmdList, ViewUniformBuffer);
				}
			}
		}
	}


	//
	//	Now Copy to staging buffer the data we want to read back (alive particle count). And make buffer ready for that and draw commands on the graphics pipe too.
	//
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, DestDataBuffers.GetData(), DestDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, DestBufferIntFloat.GetData(), DestBufferIntFloat.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestDataBuffers.GetData(), DestDataBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestBufferIntFloat.GetData(), DestBufferIntFloat.Num());
	RHICmdList.FlushComputeShaderCache();

	// We have done all our overlapping compute work on this list so go back to default behavior and flush.
	RHICmdList.AutomaticCacheFlushAfterComputeShader(true);

	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraSimReadback);
		SCOPED_GPU_STAT(RHICmdList, NiagaraSimReadback);
		for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
		{
			uint32 DispatchCount = Tick->Count;
			FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
			for (uint32 Index = 0; Index < DispatchCount; Index++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[Index];
				FNiagaraComputeExecutionContext* Context = Instance.Context;
				if (Context && Context->GPUScript_RT->GetShader())
				{
					// Don't resolve if the data if there are no instances (prevents a transition issue warning).
					if (Instance.DestinationData->GetNumInstances() > 0)
					{
						// resolve data set writes - grabs the number of instances written from the index set during the simulation run
						ResolveDatasetWrites(RHICmdList, &Instance);
					}
					check(Instance.DestinationData->GetGPUIndices().Buffer);
				}
			}
		}
	}
	// the VF grabs current state for drawing, so need to transition
	
	//TODO: Need to set the data to render in the context?

	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestBufferIntFloat.GetData(), DestBufferIntFloat.Num());

	// We have done all our compute work
	RHICmdList.FlushComputeShaderCache();
	RHICmdList.SubmitCommandsHint();
}

void NiagaraEmitterInstanceBatcher::PostRenderOpaque(FRHICommandListImmediate& RHICmdList, const FUniformBufferRHIParamRef ViewUniformBuffer, const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct, FUniformBufferRHIParamRef SceneTexturesUniformBuffer)
{
	ExecuteAll(RHICmdList, ViewUniformBuffer);
}

void NiagaraEmitterInstanceBatcher::ExecuteAll(FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer)
{
	// @todo REMOVE THIS HACK
	LastFrameThatDrainedData = GFrameNumberRenderThread;

	// This is always called by the renderer so early out if we have no work.
	if (Ticks_RT.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NiagaraEmitterInstanceBatcher_ExecuteAll);


	if (GNiagaraOverlapCompute>0)
	{
		TArray<FOverlappableTicks, TInlineAllocator<2>> SimPasses;
		{
			TMap<FNiagaraComputeExecutionContext*, FOverlappableTicks> ContextToTicks;

			// Ticks are added in order. Two tick with the same context should not overlap so should be in two different batch.
			// Those ticks should still be executed in order.
			for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
			{
				FNiagaraComputeInstanceData* data = Tick.GetInstanceData();
				ContextToTicks.FindOrAdd(data->Context).Add(&Tick);
			}

			// Count the maximum number of tick per context to know the number of passes we need to do
			uint32 NumSimPass = 0;
			for (auto& Ticks : ContextToTicks)
			{
				NumSimPass = FMath::Max(NumSimPass, (uint32)Ticks.Value.Num());
			}

			// Transpose now only once the data to get all independent tick per pass
			SimPasses.AddDefaulted(NumSimPass);
			for (auto& Ticks : ContextToTicks)
			{
				uint32 tickPass = 0;
				for (auto& Tick : Ticks.Value)
				{
					SimPasses[tickPass].Add(Tick);
					tickPass++;
				}
			}
		}

		for (auto& SimPass : SimPasses)
		{
			FNiagaraBufferArray DestDataBuffers;
			FNiagaraBufferArray CurrDataBuffers;
			FNiagaraBufferArray DestBufferIntFloat;
			FNiagaraBufferArray CurrBufferIntFloat;

			// This initial pass gathers all the buffers that are read from and written to so we can do batch resource transitions.
			// It also ensures the GPU buffers are large enough to hold everything.
			ResizeBuffersAndGatherResources(SimPass, RHICmdList, DestDataBuffers, CurrDataBuffers, DestBufferIntFloat, CurrBufferIntFloat);

			DispatchAllOnCompute(SimPass, RHICmdList, ViewUniformBuffer, DestDataBuffers, CurrDataBuffers, DestBufferIntFloat, CurrBufferIntFloat);
		}
	}
	else
	{
		for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
		{
			uint32 DispatchCount = Tick.Count;
			FNiagaraComputeInstanceData* Instances = Tick.GetInstanceData();

			for (uint32 i = 0; i < DispatchCount; i++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[i];
				TickSingle(Tick, &Instance, RHICmdList, ViewUniformBuffer);
			}
		}
	}

	FinishDispatches();
}

void NiagaraEmitterInstanceBatcher::SimStepClearAndSetup(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const
{
	check(IsInRenderingThread());
#if 1
	FNiagaraComputeExecutionContext* Context = Instance.Context;
	check(Context);


	FNiagaraShader* ComputeShader = Context->GPUScript_RT->GetShader();
	check(ComputeShader);
	if (!ComputeShader)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (Context->DebugInfo.IsValid())
	{
		ProcessDebugInfo(RHICmdList, Context);
	}
#endif // WITH_EDITORONLY_DATA

	// clear data set index buffer for the simulation shader to write number of written instances to
	//
	FRWBuffer &DatasetIndexBufferWrite = Instance.DestinationData->GetGPUIndices();
//	SCOPED_GPU_EVENTF(RHICmdList, NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraIndexBufferClear);
	SCOPED_GPU_STAT(RHICmdList, NiagaraIndexBufferClear);
	//UE_LOG(LogNiagara, Log, TEXT("Clearing UAV %p"), (FRHIVertexBuffer *)DatasetIndexBufferWrite.Buffer);
	ClearUAV(RHICmdList, DatasetIndexBufferWrite, 0);
#endif
}

//void NiagaraEmitterInstanceBatcher::SimStepReadback(const FNiagaraComputeInstanceData& Instance, FRHICommandList& RHICmdList) const
//{
//#if 1
//	FNiagaraComputeExecutionContext* Context = Instance.Context;
//	check(Context);
//	FNiagaraShader* ComputeShader = Context->GPUScript_RT->GetShader();
//	if (!ComputeShader)
//	{
//		return;
//	}
//
//	// Don't resolve if the data if there are no instances (prevents a transition issue warning).
//	if (Context->MainDataSet->CurrData().GetNumInstances() > 0)
//	{
//		// resolve data set writes - grabs the number of instances written from the index set during the simulation run
//		ResolveDatasetWrites(RHICmdList, &Instance);
//	}
//
//	check(Context->MainDataSet->HasDatasetIndices());
//#endif
//}

void NiagaraEmitterInstanceBatcher::TickSingle(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUSimTick_RT);

	FNiagaraComputeExecutionContext* Context = Instance->Context;
	
	check(IsInRenderingThread());

	FNiagaraComputeExecutionContext::TickCounter++;

	FNiagaraShader* ComputeShader = Context->GPUScript_RT->GetShader();
	if (!ComputeShader)
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (Context->DebugInfo.IsValid())
	{
		ProcessDebugInfo(RHICmdList, Context);
	}
#endif // WITH_EDITORONLY_DATA

	//The buffer containing current simulation state.
	Instance->CurrentData = Context->MainDataSet->GetCurrentData();
	//The buffer we're going to write simulation results to.
	Instance->DestinationData = &Context->MainDataSet->BeginSimulate();

	check(Instance->CurrentData && Instance->DestinationData);
	FNiagaraDataBuffer& CurrentData = *Instance->CurrentData;
	FNiagaraDataBuffer& DestinationData = *Instance->DestinationData;

	uint32 PrevNumInstances = Tick.bNeedsReset ? 0 : CurrentData.GetNumInstances();
	uint32 NewNumInstances = Instance->SpawnRateInstances + Instance->EventSpawnTotal + PrevNumInstances;

	//We must assume all particles survive when allocating here. 
	//If this is not true, the read back in ResolveDatasetWrites will shrink the buffers.
	uint32 RequiredInstances = FMath::Max(PrevNumInstances, NewNumInstances);

	DestinationData.AllocateGPU(RequiredInstances + 1, RHICmdList);
	DestinationData.SetNumInstances(RequiredInstances);

	// clear data set index buffer for the simulation shader to write number of written instances to
	{
		FRWBuffer& DatasetIndexBufferWrite = DestinationData.GetGPUIndices();
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraIndexBufferClear, TEXT("Niagara index buffer clear"));
		ClearUAV(RHICmdList, DatasetIndexBufferWrite, 0);
		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, DatasetIndexBufferWrite.UAV);
	}

	// run shader, sim and spawn in a single dispatch
	uint32 UpdateStartInstance = 0;
	Run<true>(Tick, Instance, UpdateStartInstance, NewNumInstances, ComputeShader, RHICmdList, ViewUniformBuffer);

	// ResolveDatasetWrites may read this, so we must transition it here.
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, DestinationData.GetGPUIndices().UAV);		// transition to readable; we'll be using this next frame

	// Don't resolve if the data if there are no instances (prevents a transition issue warning).
	if (NewNumInstances > 0)
	{
		// resolve data set writes - grabs the number of instances written from the index set during the simulation run
		ResolveDatasetWrites(RHICmdList, Instance);
	}

	Context->MainDataSet->EndSimulate();
	Context->SetDataToRender(Instance->DestinationData);

	/*
	// TODO: hack - only updating event set 0 on update scripts now; need to match them to their indices and update them all
	if (Context->UpdateEventWriteDataSets.Num())
	{ 
		Context->UpdateEventWriteDataSets[0]->CurrDataRender().SetNumInstances(NumInstancesAfterSim[1]);
	}
	RunEventHandlers(Context, NumInstancesAfterSim[0], NumInstancesAfterSpawn, NumInstancesAfterNonEventSpawn, RHICmdList);
	*/

	// the VF grabs PrevDataRender for drawing, so need to transition
	// Better to do this in the VF set?
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->DataToRender->GetGPUBufferFloat().UAV);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Context->DataToRender->GetGPUBufferInt().UAV);
}

void NiagaraEmitterInstanceBatcher::PreInitViews()
{
	SortedParticleCount = 0;
	SimulationsToSort.Reset();

	for (FNiagaraIndicesVertexBuffer& SortedVertexBuffer : SortedVertexBuffers)
	{
		SortedVertexBuffer.UsedIndexCount = 0;
	}
}

bool NiagaraEmitterInstanceBatcher::UsesGlobalDistanceField() const
{
	for (const FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		if (Tick.bRequiredDistanceFieldData)
		{
			return true;
		}
	}

	return false;
}

void NiagaraEmitterInstanceBatcher::PreRender(FRHICommandListImmediate& RHICmdList, const class FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData)
{
	GlobalDistanceFieldParams = GlobalDistanceFieldParameterData ? *GlobalDistanceFieldParameterData : FGlobalDistanceFieldParameterData();

	// Sort buffer after mesh batches are issued, before tick (which will change the GPU instance count).
	SortGPUParticles(RHICmdList);
}

int32 NiagaraEmitterInstanceBatcher::AddSortedGPUSimulation(const FNiagaraGPUSortInfo& SortInfo)
{
	const int32 ResultOffset = SortedParticleCount;
	SimulationsToSort.Add(SortInfo);

	SortedParticleCount += SortInfo.ParticleCount;

	if (!SortedVertexBuffers.Num())
	{
		SortedVertexBuffers.Add(new FNiagaraIndicesVertexBuffer(FMath::Max(GNiagaraGPUSortingMinBufferSize, (int32)(SortedParticleCount * GNiagaraGPUSortingBufferSlack))));
	}
	// If we don't fit anymore, reallocate to a bigger size.
	else if (SortedParticleCount > SortedVertexBuffers.Last().IndexCount)
	{
		SortedVertexBuffers.Add(new FNiagaraIndicesVertexBuffer((int32)(SortedParticleCount * GNiagaraGPUSortingBufferSlack)));
	}

	// Keep track of the last used index, which is also the first used index of next entry
	// if we need to increase the size of SortedVertexBuffers. Used in FNiagaraCopyIntBufferRegionCS
	SortedVertexBuffers.Last().UsedIndexCount = SortedParticleCount;

	return ResultOffset;
}

void NiagaraEmitterInstanceBatcher::SortGPUParticles(FRHICommandListImmediate& RHICmdList)
{
	if (SortedParticleCount > 0 && SortedVertexBuffers.Num() > 0 && SimulationsToSort.Num() && GNiagaraGPUSortingBufferSlack > 1.f)
	{
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSorting);

		ensure(SortedVertexBuffers.Last().IndexCount >= SortedParticleCount);

		// The particle sort buffer must be able to hold all the particles.
		if (SortedVertexBuffers.Last().IndexCount != ParticleSortBuffers.GetSize())
		{
			ParticleSortBuffers.ReleaseRHI();
			ParticleSortBuffers.SetBufferSize(SortedVertexBuffers.Last().IndexCount);
			ParticleSortBuffers.InitRHI();
		}

		INC_DWORD_STAT_BY(STAT_NiagaraGPUSortedParticles, SortedParticleCount);
		INC_DWORD_STAT_BY(STAT_NiagaraGPUSortedBuffers, ParticleSortBuffers.GetSize());

		// Make sure our outputs are safe to write to.
		const int32 InitialSortBufferIndex = 0;
		FRHIUnorderedAccessView* OutputUAVs[2] = { ParticleSortBuffers.GetKeyBufferUAV(InitialSortBufferIndex), ParticleSortBuffers.GetVertexBufferUAV(InitialSortBufferIndex) };
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, OutputUAVs, 2);

		// EmitterKey = (EmitterIndex & EmitterKeyMask) << EmitterKeyShift.
		// SortKey = (Key32 >> SortKeyShift) & SortKeyMask.
		uint32 EmitterKeyMask = (1 << FMath::CeilLogTwo(SimulationsToSort.Num())) - 1;
		uint32 EmitterKeyShift = 16;
		uint32 SortKeyMask = 0xFFFF;
		
		{
			SCOPED_DRAW_EVENT(RHICmdList, NiagaraSortKeyGen);

			// Bind the shader
			
			FNiagaraSortKeyGenCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FNiagaraSortKeyGenCS::FSortUsingMaxPrecision>(GNiagaraGPUSortingUseMaxPrecision != 0);
			
			TShaderMapRef<FNiagaraSortKeyGenCS> KeyGenCS(GetGlobalShaderMap(FeatureLevel), PermutationVector);
			RHICmdList.SetComputeShader(KeyGenCS->GetComputeShader());
			KeyGenCS->SetOutput(RHICmdList, ParticleSortBuffers.GetKeyBufferUAV(InitialSortBufferIndex), ParticleSortBuffers.GetVertexBufferUAV(InitialSortBufferIndex));

			// (SortKeyMask, SortKeyShift, SortKeySignBit)
			FUintVector4 SortKeyParams(SortKeyMask, 0, 0x8000, 0); 
			if (GNiagaraGPUSortingUseMaxPrecision != 0)
			{
				EmitterKeyMask = FMath::Max<uint32>(EmitterKeyMask , 1); // Need at list 1 bit for the above logic
				uint32 UnusedBits = FPlatformMath::CountLeadingZeros(EmitterKeyMask << EmitterKeyShift);
				EmitterKeyShift += UnusedBits;
				SortKeyMask = ~(EmitterKeyMask << EmitterKeyShift);

				SortKeyParams.X = SortKeyMask;
				SortKeyParams.Y = 16 - UnusedBits;
				SortKeyParams.Z <<= UnusedBits;
			}
			
			int32 OutputOffset = 0;
			for (int32 EmitterIndex = 0; EmitterIndex < SimulationsToSort.Num(); ++EmitterIndex)
			{
				const FNiagaraGPUSortInfo& SortInfo = SimulationsToSort[EmitterIndex];
				KeyGenCS->SetParameters(RHICmdList, SortInfo, (uint32)EmitterIndex << EmitterKeyShift, OutputOffset, SortKeyParams);
				DispatchComputeShader(RHICmdList, *KeyGenCS, FMath::DivideAndRoundUp(SortInfo.ParticleCount, NIAGARA_KEY_GEN_THREAD_COUNT), 1, 1);

				OutputOffset += SortInfo.ParticleCount;
			}
			KeyGenCS->UnbindBuffers(RHICmdList);
		}

		// We may be able to remove this transition if each step isn't dependent on the previous one.
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, OutputUAVs, 2);

		// Sort buffers and copy results to index buffers.
		{
			const uint32 KeyMask = (EmitterKeyMask << EmitterKeyShift) | SortKeyMask;
			const int32 ResultBufferIndex = SortGPUBuffers(RHICmdList, ParticleSortBuffers.GetSortBuffers(), InitialSortBufferIndex, KeyMask, SortedParticleCount, FeatureLevel);
			ResolveParticleSortBuffers(RHICmdList, ResultBufferIndex);
		}

		// Only keep the last sorted index buffer, which is of the same size as ParticleSortBuffers.GetSize().
		SortedVertexBuffers.RemoveAt(0, SortedVertexBuffers.Num() - 1);

		// Resize the buffer to maximize next frame.
		// Those ratio must take into consideration the slack ratio to be stable.

		const int32 RecommandedSize = FMath::Max(GNiagaraGPUSortingMinBufferSize, (int32)(SortedParticleCount * GNiagaraGPUSortingBufferSlack));
		const float BufferUsage = (float)SortedParticleCount /	(float)ParticleSortBuffers.GetSize();

		if (RecommandedSize < ParticleSortBuffers.GetSize() / GNiagaraGPUSortingBufferSlack)
		{
			if (NumFramesRequiringShrinking >= GNiagaraGPUSortingFrameCountBeforeBufferShrinking)
			{
				NumFramesRequiringShrinking = 0;
				ParticleSortBuffers.ReleaseRHI();
				ParticleSortBuffers.SetBufferSize(0);

				// Add an entry that should fit well for next frame.
				SortedVertexBuffers.Empty();
				SortedVertexBuffers.Add(new FNiagaraIndicesVertexBuffer(RecommandedSize));
			}
			else
			{
				++NumFramesRequiringShrinking;
			}
		}
	}
	else // If the are no sort task, we don't need any of the sort buffers.
	{
		if (NumFramesRequiringShrinking >= GNiagaraGPUSortingFrameCountBeforeBufferShrinking)
		{
			NumFramesRequiringShrinking = 0;
			ParticleSortBuffers.ReleaseRHI();
			ParticleSortBuffers.SetBufferSize(0);
			SortedVertexBuffers.Empty();
		}
		else
		{
			++NumFramesRequiringShrinking;
		}
	}
}

void NiagaraEmitterInstanceBatcher::ResolveParticleSortBuffers(FRHICommandListImmediate& RHICmdList, int32 ResultBufferIndex)
{
	SCOPED_DRAW_EVENT(RHICmdList, NiagaraResolveParticleSortBuffers);

#if 0 // TODO use this once working properly!
	if (SortedVertexBuffers.Num() == 1)
	{
		RHICmdList.CopyVertexBuffer(ParticleSortBuffers.GetSortedVertexBufferRHI(ResultBufferIndex), SortedVertexBuffers.Last().VertexBufferRHI);
		return;
	}
#endif

	TShaderMapRef<FNiagaraCopyIntBufferRegionCS> CopyBufferCS(GetGlobalShaderMap(FeatureLevel));
	RHICmdList.SetComputeShader(CopyBufferCS->GetComputeShader());

	int32 StartingIndex = 0;

	for (int32 Index = 0; Index < SortedVertexBuffers.Num(); Index += NIAGARA_COPY_BUFFER_BUFFER_COUNT)
	{
		FRHIUnorderedAccessView* UAVs[NIAGARA_COPY_BUFFER_BUFFER_COUNT] = {};
		int32 UsedIndexCounts[NIAGARA_COPY_BUFFER_BUFFER_COUNT] = {};

		const int32 NumBuffers = FMath::Min<int32>(NIAGARA_COPY_BUFFER_BUFFER_COUNT, SortedVertexBuffers.Num() - Index);

		int32 LastCount = StartingIndex;
		for (int32 SubIndex = 0; SubIndex < NumBuffers; ++SubIndex)
		{
			const FNiagaraIndicesVertexBuffer& SortBuffer = SortedVertexBuffers[Index + SubIndex];
			UAVs[SubIndex] = SortBuffer.VertexBufferUAV;
			UsedIndexCounts[SubIndex] = SortBuffer.UsedIndexCount;

			LastCount = SortBuffer.UsedIndexCount;
		}

		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, UAVs, NumBuffers);

		CopyBufferCS->SetParameters(RHICmdList, ParticleSortBuffers.GetSortedVertexBufferSRV(ResultBufferIndex), UAVs, UsedIndexCounts, StartingIndex, NumBuffers);
		DispatchComputeShader(RHICmdList, *CopyBufferCS, FMath::DivideAndRoundUp(LastCount - StartingIndex, NIAGARA_COPY_BUFFER_THREAD_COUNT), 1, 1);
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToGfx, UAVs, NumBuffers);

		StartingIndex = LastCount;
	}
	CopyBufferCS->UnbindBuffers(RHICmdList);
}

/* Attempt to read back simulation results (number of live instances) from the GPU via an async readback request; 
 *	If the readback isn't ready to be performed, we accumulate spawn rates and assume all instances have survived, until
 *	the GPU can tell us how many are actually alive; since that data may be several frames old, we'll always end up
 *	overallocating a bit, and the CPU might think we have more particles alive than we actually do;
 *	since we use DrawIndirect with the GPU determining draw call parameters, that's not an issue
 */
void NiagaraEmitterInstanceBatcher::ResolveDatasetWrites(FRHICommandList &RHICmdList, FNiagaraComputeInstanceData *Instance) const
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;
	FNiagaraDataBuffer& DestinationData = *Instance->DestinationData;
	FRWBuffer &DatasetIndexBufferWrite = DestinationData.GetGPUIndices();
	uint32 SpawnedThisFrame = Instance->SpawnRateInstances + Instance->EventSpawnTotal;
	Context->AccumulatedSpawnRate += SpawnedThisFrame;
	int32 ExistingDataCount = DestinationData.GetNumInstances();
	if (!Context->GPUDataReadback)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraAllocateGPUReadback_RT);

		Context->GPUDataReadback = new FRHIGPUBufferReadback(TEXT("Niagara GPU Emitter Readback"));
		INC_DWORD_STAT(STAT_NiagaraReadbackLatency);
		Context->GPUDataReadback->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, ExistingDataCount);
	}
	else if (Context->GPUDataReadback->IsReady())
	{
		bool bSuccessfullyRead = false;
		{
		    SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUReadback_RT);
		    int32 *NumInstancesAfterSim = static_cast<int32*>(Context->GPUDataReadback->Lock(64 * sizeof(int32)));
			if (NumInstancesAfterSim)
			{
				int32 NewExistingDataCount = (Context->ResetSinceLastReadbackIssued ? 0 : NumInstancesAfterSim[1]) + Context->AccumulatedSpawnRate; // index 1 is always the count
				DestinationData.SetNumInstances(NewExistingDataCount);
				//UE_LOG(LogNiagara, Log, TEXT("GPU Syncup %s : Was(%d) Now(%d) Accumulated(%d) NumInstancesAfterSim[1](%d)"), *Context->DebugSimName, ExistingDataCount, NewExistingDataCount , Context->AccumulatedSpawnRate, NumInstancesAfterSim[1]);
				//UE_LOG(LogNiagara, Log, TEXT("Reading UAV %p"), (FRHIVertexBuffer *)DatasetIndexBufferWrite.Buffer);

				INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, NewExistingDataCount);
				SET_DWORD_STAT(STAT_NiagaraReadbackLatency, 0);

				Context->AccumulatedSpawnRate = 0;
				Context->ResetSinceLastReadbackIssued = false;
				bSuccessfullyRead = true;
			}
			else
			{
				UE_LOG(LogNiagara, Warning, TEXT("GPUDataReadback said it was ready, but returned an invalid buffer. Skipping this time.."));
				INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, ExistingDataCount);
			}
			Context->GPUDataReadback->Unlock();
		}
		if (bSuccessfullyRead)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraAllocateGPUReadback_RT);
			// The following code seems to take significant time on d3d12
			Context->GPUDataReadback->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		}
	}
	else
	{
		INC_DWORD_STAT_BY(STAT_NiagaraGPUParticles, ExistingDataCount);
	}
}

void NiagaraEmitterInstanceBatcher::ProcessDebugInfo(FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext* Context) const
{
#if WITH_EDITORONLY_DATA
	// This method may be called from one of two places: in the tick or as part of a paused frame looking for the debug info that was submitted previously...
	// Note that PrevData is where we expect the data to be for rendering, as per NiagaraEmitterInstanceBatcher::TickSingle
	if (Context && Context->DebugInfo.IsValid())
	{
		// Fire off the readback if not already doing so
		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackCounts)
		{
			// Do nothing.., handled in Run
		}
		// We may not have floats or ints, but we should have at least one of the two
		else if ((Context->GPUDebugDataReadbackFloat == nullptr || Context->GPUDebugDataReadbackFloat->IsReady()) 
				&& (Context->GPUDebugDataReadbackInt == nullptr || Context->GPUDebugDataReadbackInt->IsReady())
				&& Context->GPUDebugDataReadbackCounts->IsReady()
			)
		{
			//UE_LOG(LogNiagara, Warning, TEXT("Read back!"));

			int32 *NumInstancesAfterSim = static_cast<int32*>(Context->GPUDebugDataReadbackCounts->Lock(64 * sizeof(int32)));
			int32 NewExistingDataCount = NumInstancesAfterSim[1];
			{
				float* FloatDataBuffer = nullptr;
				if (Context->GPUDebugDataReadbackFloat)
				{
					FloatDataBuffer = static_cast<float*>(Context->GPUDebugDataReadbackFloat->Lock(Context->GPUDebugDataFloatSize));
				}
				int* IntDataBuffer = nullptr;
				if (Context->GPUDebugDataReadbackInt)
				{
					IntDataBuffer = static_cast<int*>(Context->GPUDebugDataReadbackInt->Lock(Context->GPUDebugDataIntSize));
				}

				Context->DebugInfo->Frame.CopyFromGPUReadback(FloatDataBuffer, IntDataBuffer, 0, NewExistingDataCount, Context->GPUDebugDataFloatStride, Context->GPUDebugDataIntStride);

				Context->DebugInfo->bWritten = true;

				if (Context->GPUDebugDataReadbackFloat)
				{
					Context->GPUDebugDataReadbackFloat->Unlock();
				}
				if (Context->GPUDebugDataReadbackInt)
				{
					Context->GPUDebugDataReadbackInt->Unlock();
				}
				Context->GPUDebugDataReadbackCounts->Unlock();
			}
			{
				// The following code seems to take significant time on d3d12
				// Clear out the readback buffers...
				if (Context->GPUDebugDataReadbackFloat)
				{
					delete Context->GPUDebugDataReadbackFloat;
					Context->GPUDebugDataReadbackFloat = nullptr;
				}
				if (Context->GPUDebugDataReadbackInt)
				{
					delete Context->GPUDebugDataReadbackInt;
					Context->GPUDebugDataReadbackInt = nullptr;
				}
				delete Context->GPUDebugDataReadbackCounts;
				Context->GPUDebugDataReadbackCounts = nullptr;	
				Context->GPUDebugDataFloatSize = 0;
				Context->GPUDebugDataIntSize = 0;
				Context->GPUDebugDataFloatStride = 0;
				Context->GPUDebugDataIntStride = 0;
			}

			// We've updated the debug info directly, now we need to no longer keep asking and querying because this frame is done!
			Context->DebugInfo.Reset();
		}
	}
#endif // WITH_EDITORONLY_DATA
}

/* Set shader parameters for data interfaces
 */
void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FGuid& SystemInstance = Tick.SystemInstanceID;

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			FNiagaraDataInterfaceSetArgs Context;
			Context.Shader = Shader;
			Context.DataInterface = Interface;
			Context.SystemInstance = SystemInstance;
			Context.Batcher = this;
			DIParam.Parameters->Set(RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*> &DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FGuid& SystemInstance = Tick.SystemInstanceID;

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			void* PerInstanceData = nullptr;
			int32* OffsetFound = nullptr;
			if (Tick.DIInstanceData && Tick.DIInstanceData->PerInstanceDataSize != 0 && Tick.DIInstanceData->InterfaceProxiesToOffsets.Num() != 0)
			{
				OffsetFound = Tick.DIInstanceData->InterfaceProxiesToOffsets.Find(Interface);
				if (OffsetFound != nullptr)
				{
					PerInstanceData = (*OffsetFound) + (uint8*)Tick.DIInstanceData->PerInstanceDataForRT;
				}
			}
			FNiagaraDataInterfaceSetArgs Context;
			Context.Shader = Shader;
			Context.DataInterface = Interface;
			Context.SystemInstance = SystemInstance;
			Context.Batcher = this;
			DIParam.Parameters->Unset(RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}


/* Kick off a simulation/spawn run
 */
 template<bool bDoResourceTransitions>
void NiagaraEmitterInstanceBatcher::Run(const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData* Instance, uint32 UpdateStartInstance, const uint32 TotalNumInstances, FNiagaraShader* Shader,
	FRHICommandList &RHICmdList, FUniformBufferRHIParamRef ViewUniformBuffer, bool bCopyBeforeStart) const
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;
	if (TotalNumInstances == 0 && !bDoResourceTransitions)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u"),
			*Context->DebugSimName,
			TotalNumInstances);
		return;
	}

	//UE_LOG(LogNiagara, Warning, TEXT("Run"));
	
	const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies = Instance->DataInterfaceProxies;
	const FRHIUniformBufferLayout& CBufferLayout = Context->CBufferLayout;
	check(Instance->CurrentData && Instance->DestinationData);
	FNiagaraDataBuffer& DestinationData = *Instance->DestinationData;
	FNiagaraDataBuffer& CurrentData = *Instance->CurrentData;

	const FRWBuffer& WriteIndexBuffer = DestinationData.GetGPUIndices();
	FRWBuffer& ReadIndexBuffer = CurrentData.GetGPUIndices();

	// if we don't have a previous index buffer, we need to prep one using the maximum number of instances; this should only happen on the first frame
	//		the data set index buffer is really the param buffer for the indirect draw call; it contains the number of live instances at index 1, and the simulation
	//		CS uses this to determine the current number of active instances in the buffer
	//
	if (ReadIndexBuffer.Buffer == nullptr)
	{
		TResourceArray<int32> InitIndexBuffer;
		InitIndexBuffer.AddUninitialized(64);
		InitIndexBuffer[1] = 0;		// number of instances 
		ReadIndexBuffer.Initialize(sizeof(int32), 64, EPixelFormat::PF_R32_UINT, BUF_DrawIndirect | BUF_Static, nullptr, &InitIndexBuffer);
	}
	else if (Tick.bNeedsReset)
	{
		//UE_LOG(LogNiagara, Log, TEXT("Clearing UAV due to reset!"));
		ClearUAV(RHICmdList, ReadIndexBuffer, 0);
		Context->AccumulatedSpawnRate = 0;
		Context->ResetSinceLastReadbackIssued = true;
	}

	RHICmdList.SetComputeShader(Shader->GetComputeShader());

	RHICmdList.SetShaderResourceViewParameter(Shader->GetComputeShader(), Shader->InputIndexBufferParam.GetBaseIndex(), ReadIndexBuffer.SRV);

	// set the view uniform buffer param
	if (Shader->ViewUniformBufferParam.IsBound() && ViewUniformBuffer)
	{
		RHICmdList.SetShaderUniformBuffer(Shader->GetComputeShader(), Shader->ViewUniformBufferParam.GetBaseIndex(), ViewUniformBuffer);
	}

	SetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick);

	// set the shader and data set params 
	//
	CurrentData.SetShaderParams<bDoResourceTransitions>(Shader, RHICmdList, true);
	DestinationData.SetShaderParams<bDoResourceTransitions>(Shader, RHICmdList, false);

	// set the index buffer uav
	//
	if (Shader->OutputIndexBufferParam.IsBound())
	{
		RHICmdList.SetUAVParameter(Shader->GetComputeShader(), Shader->OutputIndexBufferParam.GetUAVIndex(), WriteIndexBuffer.UAV);
	}

	// set the execution parameters
	//
	if (Shader->EmitterTickCounterParam.IsBound())
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->EmitterTickCounterParam.GetBufferIndex(), Shader->EmitterTickCounterParam.GetBaseIndex(), Shader->EmitterTickCounterParam.GetNumBytes(), &FNiagaraComputeExecutionContext::TickCounter);
	}

	uint32 Copy = bCopyBeforeStart ? 1 : 0;

	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->UpdateStartInstanceParam.GetBufferIndex(), Shader->UpdateStartInstanceParam.GetBaseIndex(), Shader->UpdateStartInstanceParam.GetNumBytes(), &UpdateStartInstance);					// 0, except for event handler runs
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->NumIndicesPerInstanceParam.GetBufferIndex(), Shader->NumIndicesPerInstanceParam.GetBaseIndex(), Shader->NumIndicesPerInstanceParam.GetNumBytes(), &Context->NumIndicesPerInstance);		// set from the renderer in FNiagaraEmitterInstance::Tick
	int32 InstancesToSpawnThisFrame = Instance->SpawnRateInstances + Instance->EventSpawnTotal;
	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->NumSpawnedInstancesParam.GetBufferIndex(), Shader->NumSpawnedInstancesParam.GetBaseIndex(), Shader->NumSpawnedInstancesParam.GetNumBytes(), &InstancesToSpawnThisFrame);				// number of instances in the spawn run

	uint32 NumThreadGroups = 1;
	if (TotalNumInstances > NIAGARA_COMPUTE_THREADGROUP_SIZE)
	{
		NumThreadGroups = FMath::Min(NIAGARA_MAX_COMPUTE_THREADGROUPS, FMath::DivideAndRoundUp(TotalNumInstances, NIAGARA_COMPUTE_THREADGROUP_SIZE));
	}

	// setup script parameters
	if (CBufferLayout.ConstantBufferSize)
	{
		check(CBufferLayout.Resources.Num() == 0);
		const uint8* ParamData = Instance->ParamData;
		FUniformBufferRHIRef CBuffer = RHICreateUniformBuffer(ParamData, CBufferLayout, EUniformBufferUsage::UniformBuffer_SingleDraw);
		RHICmdList.SetShaderUniformBuffer(Shader->GetComputeShader(), Shader->EmitterConstantBufferParam.GetBaseIndex(), CBuffer);
	}
	else
	{
		ensure(!Shader->EmitterConstantBufferParam.IsBound());
	}

	// Dispatch, if anything needs to be done
	//
	if (TotalNumInstances)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u"),
			*Context->DebugSimName,
			TotalNumInstances);
		DispatchComputeShader(RHICmdList, Shader, NumThreadGroups, 1, 1);
	}

#if WITH_EDITORONLY_DATA
	// Check to see if we need to queue up a debug dump..
	if (Context->DebugInfo.IsValid())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Queued up!"));

		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackCounts)
		{
			FRWBuffer &DatasetIndexBufferWrite = DestinationData.GetGPUIndices();

			Context->GPUDebugDataFloatSize = 0;
			Context->GPUDebugDataIntSize = 0;
			Context->GPUDebugDataFloatStride = 0;
			Context->GPUDebugDataIntStride = 0;

			if (DestinationData.GetGPUBufferFloat().NumBytes > 0)
			{
				static const FName ReadbackFloatName(TEXT("Niagara GPU Debug Info Float Emitter Readback"));
				Context->GPUDebugDataReadbackFloat = new FRHIGPUBufferReadback(ReadbackFloatName);
				Context->GPUDebugDataReadbackFloat->EnqueueCopy(RHICmdList, DestinationData.GetGPUBufferFloat().Buffer);
				Context->GPUDebugDataFloatSize = DestinationData.GetGPUBufferFloat().NumBytes;
				Context->GPUDebugDataFloatStride = DestinationData.GetFloatStride();
			}

			if (DestinationData.GetGPUBufferInt().NumBytes > 0)
			{
				static const FName ReadbackIntName(TEXT("Niagara GPU Debug Info Int Emitter Readback"));
				Context->GPUDebugDataReadbackInt = new FRHIGPUBufferReadback(ReadbackIntName);
				Context->GPUDebugDataReadbackInt->EnqueueCopy(RHICmdList, DestinationData.GetGPUBufferInt().Buffer);
				Context->GPUDebugDataIntSize = DestinationData.GetGPUBufferInt().NumBytes;
				Context->GPUDebugDataIntStride = DestinationData.GetInt32Stride();
			}

			static const FName ReadbackCountsName(TEXT("Niagara GPU Emitter Readback"));
			Context->GPUDebugDataReadbackCounts = new FRHIGPUBufferReadback(ReadbackCountsName);
			Context->GPUDebugDataReadbackCounts->EnqueueCopy(RHICmdList, DatasetIndexBufferWrite.Buffer);
		}
	}
#endif // WITH_EDITORONLY_DATA

	// Unset UAV parameters and transition resources (TODO: resource transition should be moved to the renderer)
	// 
	UnsetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick);
	CurrentData.UnsetShaderParams(Shader, RHICmdList);
	DestinationData.UnsetShaderParams(Shader, RHICmdList);
	Shader->OutputIndexBufferParam.UnsetUAV(RHICmdList, Shader->GetComputeShader());
}
