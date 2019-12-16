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
DECLARE_FLOAT_COUNTER_STAT(TEXT("Niagara GPU Sim"), STAT_GPU_NiagaraSim, STATGROUP_GPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Particles"), STAT_NiagaraGPUParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Particles"), STAT_NiagaraGPUSortedParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Buffers"), STAT_NiagaraGPUSortedBuffers, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Readback latency (frames)"), STAT_NiagaraReadbackLatency, STATGROUP_Niagara);

DECLARE_GPU_STAT_NAMED(NiagaraGPU, TEXT("Niagara"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSimulation, TEXT("Niagara GPU Simulation"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSorting, TEXT("Niagara GPU sorting"));

uint32 FNiagaraComputeExecutionContext::TickCounter = 0;

int32 GNiagaraAllowTickBeforeRender = 1;
static FAutoConsoleVariableRef CVarNiagaraAllowTickBeforeRender(
	TEXT("fx.NiagaraAllowTickBeforeRender"),
	GNiagaraAllowTickBeforeRender,
	TEXT("If 1, Niagara GPU systems that don't rely on view data will be rendered in sync\n")
	TEXT("with the current frame simulation instead of the last frame one. (default=1)\n"),
	ECVF_Default
);

int32 GNiagaraOverlapCompute = 1;
static FAutoConsoleVariableRef CVarNiagaraUseAsyncCompute(
	TEXT("fx.NiagaraOverlapCompute"),
	GNiagaraOverlapCompute,
	TEXT("0 - Disable compute dispatch overlap, this will result in poor performance due to resource barriers between each dispatch call, but can be used to debug resource transition issues.\n")
	TEXT("1 - (Default) Enable compute dispatch overlap where possible, this increases GPU utilization.\n"),
	ECVF_Default
);

int32 GNiagaraSubmitCommands = 0;
static FAutoConsoleVariableRef CVarNiagaraSubmitCommands(
	TEXT("fx.NiagaraSubmitCommands"),
	GNiagaraSubmitCommands,
	TEXT("1 - (Default) Submit commands to the GPU once we have finished dispatching.\n"),
	ECVF_Default
);

// @todo REMOVE THIS HACK
int32 GNiagaraGpuMaxQueuedRenderFrames = 10;
static FAutoConsoleVariableRef CVarNiagaraGpuMaxQueuedRenderFrames(
	TEXT("fx.NiagaraGpuMaxQueuedRenderFrames"),
	GNiagaraGpuMaxQueuedRenderFrames,
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
	FinishDispatches();
	ParticleSortBuffers.ReleaseRHI();
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
	LLM_SCOPE(ELLMTag::Niagara);
	ContextsToDestroy_RT.Add(Context);
}

void NiagaraEmitterInstanceBatcher::GiveDataSetToDestroy_RenderThread(FNiagaraDataSet* DataSet)
{
	LLM_SCOPE(ELLMTag::Niagara);
	DataSetsToDestroy_RT.Add(DataSet);
}

void NiagaraEmitterInstanceBatcher::FinishDispatches()
{
	ReleaseTicks();

	for (FNiagaraComputeExecutionContext* Context : ContextsToDestroy_RT)
	{
		check(Context);
		// Put back the GPU instance counter the global pool.
		GPUInstanceCounterManager.FreeEntry(Context->EmitterInstanceReadback.GPUCountOffset);
		delete Context;
	}
	ContextsToDestroy_RT.Reset();

	for (FNiagaraDataSet* DataSet : DataSetsToDestroy_RT)
	{
		check(DataSet);
		// Put back the GPU instance counter the global pool.
		DataSet->ReleaseGPUInstanceCounts(GPUInstanceCounterManager);
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

bool NiagaraEmitterInstanceBatcher::ResetDataInterfaces(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FNiagaraShader* ComputeShader ) const
{
	bool ValidSpawnStage = true;
	FNiagaraComputeExecutionContext* Context = Instance->Context;

	// Reset all rw data interface data
	if (Tick.bNeedsReset)
	{
		uint32 InterfaceIndex = 0;
		for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
		{
			FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
			if (DIParam.Parameters)
			{
				FNiagaraDataInterfaceSetArgs TmpContext;
				TmpContext.Shader = ComputeShader;
				TmpContext.DataInterface = Interface;
				TmpContext.SystemInstance = Tick.SystemInstanceID;
				TmpContext.Batcher = this;		
				Interface->ResetData(RHICmdList, TmpContext);
			}			
			InterfaceIndex++;
		}
	}
	return ValidSpawnStage;
}

FNiagaraDataInterfaceProxy* NiagaraEmitterInstanceBatcher::FindIterationInterface( FNiagaraComputeInstanceData *Instance, const uint32 ShaderStageIndex) const
{
	// Determine if the iteration is outputting to a custom data size
	FNiagaraDataInterfaceProxy* IterationInterface = nullptr;

	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		if (Interface->IsIterationStage(ShaderStageIndex))
		{
			if (IterationInterface)
			{
				UE_LOG(LogNiagara, Error, TEXT("Multiple output Data Interfaces found for current stage"));
			}
			else
			{
				IterationInterface = Interface;
			}
		}
	}
	return IterationInterface;
}

void NiagaraEmitterInstanceBatcher::PreStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FNiagaraShader* ComputeShader, const uint32 ShaderStageIndex) const
{
	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			FNiagaraDataInterfaceSetArgs TmpContext;
			TmpContext.Shader = ComputeShader;
			TmpContext.DataInterface = Interface;
			TmpContext.SystemInstance = Tick.SystemInstanceID;
			TmpContext.Batcher = this;
			TmpContext.ShaderStageIndex = ShaderStageIndex;
			TmpContext.IsOutputStage = Interface->IsOutputStage(ShaderStageIndex);
			TmpContext.IsIterationStage = Interface->IsIterationStage(ShaderStageIndex);
			Interface->PreStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::PostStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FNiagaraShader* ComputeShader, const uint32 ShaderStageIndex) const
{
	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters)
		{
			FNiagaraDataInterfaceSetArgs TmpContext;
			TmpContext.Shader = ComputeShader;
			TmpContext.DataInterface = Interface;
			TmpContext.SystemInstance = Tick.SystemInstanceID;
			TmpContext.Batcher = this;
			TmpContext.ShaderStageIndex = ShaderStageIndex;
			TmpContext.IsOutputStage = Interface->IsOutputStage(ShaderStageIndex);
			TmpContext.IsIterationStage = Interface->IsIterationStage(ShaderStageIndex);
			Interface->PostStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::DispatchMultipleStages(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, FNiagaraShader* ComputeShader) const
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;

	if (!ResetDataInterfaces(Tick, Instance, RHICmdList, ComputeShader)) return;

	static const auto UseShaderStagesCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.UseShaderStages"));
	if (UseShaderStagesCVar->GetInt() == 1)
	{

		bool HasRunParticleStage = false;

		FNiagaraDataBuffer* CurrentData = Instance->CurrentData;
		FNiagaraDataBuffer* DestinationData = Instance->DestinationData;

		const uint32 NumStages = Instance->Context->MaxUpdateIterations;
		const uint32 DefaultShaderStageIndex = Instance->Context->DefaultShaderStageIndex;
		bool bTransitionCurrentBuffer = false;

		for (uint32 ShaderStageIndex = 0; ShaderStageIndex < NumStages; ++ShaderStageIndex)
		{
			// Determine if the iteration is outputting to a custom data size
			FNiagaraDataInterfaceProxy *IterationInterface = FindIterationInterface(Instance, ShaderStageIndex);

			if (IterationInterface && Context->SpawnStages.Num() > 0 &&
				((Tick.bNeedsReset && !Context->SpawnStages.Contains(ShaderStageIndex)) ||
				(!Tick.bNeedsReset && Context->SpawnStages.Contains(ShaderStageIndex))))
			{
				continue;
			}

			PreStageInterface(Tick, Instance, RHICmdList, ComputeShader, ShaderStageIndex);

			// If we are reading from current data we need to transition the resource if it was previously written to
			if (bTransitionCurrentBuffer && (ComputeShader->FloatInputBufferParam.IsBound() || ComputeShader->IntInputBufferParam.IsBound()))
			{
				bTransitionCurrentBuffer = false;

				TArray<FRHIUnorderedAccessView*, TInlineAllocator<2>> Resources;
				if (Instance->CurrentData->GetGPUBufferFloat().UAV.IsValid())
				{
					Resources.Add(Instance->CurrentData->GetGPUBufferFloat().UAV);
				}
				if (Instance->CurrentData->GetGPUBufferInt().UAV.IsValid())
				{
					Resources.Add(Instance->CurrentData->GetGPUBufferInt().UAV);
				}
				if (Resources.Num() > 0)
				{
					RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, Resources.GetData(), Resources.Num());
				}
			}

			if (!IterationInterface)
			{
				Run(Tick, Instance, 0, Instance->DestinationData->GetNumInstances(), ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo, false, DefaultShaderStageIndex, ShaderStageIndex,  nullptr, HasRunParticleStage);
				HasRunParticleStage = true;
				bTransitionCurrentBuffer = true;

				FNiagaraDataBuffer* StoreData = Instance->CurrentData;
				Instance->CurrentData = Instance->DestinationData;
				Instance->DestinationData = StoreData;
			}
			else
			{
				// run with correct number of instances.  This will make curr data junk or empty
				Run(Tick, Instance, 0, IterationInterface->ElementCount, ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo, false, DefaultShaderStageIndex, ShaderStageIndex, IterationInterface);
			}
			PostStageInterface(Tick, Instance, RHICmdList, ComputeShader, ShaderStageIndex);
		}
		Instance->CurrentData = CurrentData;
		Instance->DestinationData = DestinationData;
	}
	else
	{
		// run shader, sim and spawn in a single dispatch
		Run(Tick, Instance, 0, Instance->DestinationData->GetNumInstances(), ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo);
	}
}

void NiagaraEmitterInstanceBatcher::ResizeBuffersAndGatherResources(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FNiagaraBufferArray& DestDataBuffers, FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& DestBufferIntFloat, FNiagaraBufferArray& CurrBufferIntFloat)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUDispatchSetup_RT);

	//UE_LOG(LogNiagara, Warning, TEXT("NiagaraEmitterInstanceBatcher::ResizeBuffersAndGatherResources:  %0xP"), this);
	for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
	{
		const uint32 DispatchCount = Tick->Count;
		const bool bIsFinalTick = Tick->bIsFinalTick;
		const bool bNeedsReset = Tick->bNeedsReset;

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

			const uint32 PrevNumInstances = bNeedsReset ? 0 : CurrentData.GetNumInstances();
			const uint32 NewNumInstances = Instance.SpawnInfo.SpawnRateInstances + Instance.SpawnInfo.EventSpawnTotal + PrevNumInstances;

			//We must assume all particles survive when allocating here. 
			//If this is not true, the read back in ResolveDatasetWrites will shrink the buffers.
			const uint32 RequiredInstances = FMath::Max(PrevNumInstances, NewNumInstances);
			const uint32 AllocatedInstances = FMath::Max(RequiredInstances, Instance.SpawnInfo.MaxParticleCount);

			DestinationData.AllocateGPU(AllocatedInstances + 1, GPUInstanceCounterManager, RHICmdList);
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
			if (bIsFinalTick)
			{
				Context->SetDataToRender(Instance.DestinationData);
			}
		}
	}
}

void NiagaraEmitterInstanceBatcher::DispatchAllOnCompute(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, FNiagaraBufferArray& DestDataBuffers, FNiagaraBufferArray& CurrDataBuffers, FNiagaraBufferArray& DestBufferIntFloat, FNiagaraBufferArray& CurrBufferIntFloat, bool bSetReadback)
{
	FRHICommandListImmediate& RHICmdListImmediate = FRHICommandListExecutor::GetImmediateCommandList();

	//UE_LOG(LogNiagara, Warning, TEXT("NiagaraEmitterInstanceBatcher::DispatchAllOnCompute:  %0xP"), this);
	// Disable automatic cache flush so that we can have our compute work overlapping. Barrier will be used as a sync mechanism.
	RHICmdList.AutomaticCacheFlushAfterComputeShader(false);

#if WITH_EDITORONLY_DATA
	{
		//
		//	Transition current index buffer ready for compute and clear then all using overlapping compute work items.
		//
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, CurrDataBuffers.GetData(), CurrDataBuffers.Num());

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
					if (Context->DebugInfo.IsValid())
					{
						ProcessDebugInfo(RHICmdList, Context);
					}
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA

	//
	//	Add a rw barrier for the destination data buffers we just cleared and mark others as read/write as needed for particles simulation.
	//
	{
		TArray<FRHIUnorderedAccessView*, TInlineAllocator<5>> TransitionUAVs;
		TransitionUAVs.Add(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
		TransitionUAVs.Append(DestDataBuffers.GetData(), DestDataBuffers.Num());
		TransitionUAVs.Append(DestBufferIntFloat.GetData(), DestBufferIntFloat.Num());
		TransitionUAVs.Append(CurrDataBuffers.GetData(), CurrDataBuffers.Num());
		TransitionUAVs.Append(CurrBufferIntFloat.GetData(), CurrBufferIntFloat.Num());
		RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EGfxToCompute, TransitionUAVs.GetData(), TransitionUAVs.Num());
	}

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
				DispatchMultipleStages(*Tick, &Instance, RHICmdList, ViewUniformBuffer, Context->GPUScript_RT->GetShader());

				FNiagaraDataBuffer* CurrentData = Instance.CurrentData;
				if (bSetReadback && Tick->bIsFinalTick)
				{
					// Now that the current data is not required anymore, stage it for readback.
					if (CurrentData->GetNumInstances() && Context->EmitterInstanceReadback.GPUCountOffset == INDEX_NONE && CurrentData->GetGPUInstanceCountBufferOffset() != INDEX_NONE)
					{
						// Transfer the GPU instance counter ownership to the context. Note that when bSetReadback is true, a readback request will be performed later in the tick update.
						Context->EmitterInstanceReadback.GPUCountOffset = CurrentData->GetGPUInstanceCountBufferOffset();
						Context->EmitterInstanceReadback.CPUCount = CurrentData->GetNumInstances();
						CurrentData->ClearGPUInstanceCountBufferOffset();
					}
				}
			}
		}
	}

	//
	//	Now Copy to staging buffer the data we want to read back (alive particle count). And make buffer ready for that and draw commands on the graphics pipe too.
	//
	{
		TArray<FRHIUnorderedAccessView*, TInlineAllocator<5>> TransitionUAVs;
		TransitionUAVs.Add(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
		TransitionUAVs.Append(DestDataBuffers.GetData(), DestDataBuffers.Num());
		TransitionUAVs.Append(DestBufferIntFloat.GetData(), DestBufferIntFloat.Num());
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, TransitionUAVs.GetData(), TransitionUAVs.Num());
	}
	// We have done all our overlapping compute work on this list so go back to default behavior and flush.
	RHICmdList.AutomaticCacheFlushAfterComputeShader(true);

	if (GNiagaraSubmitCommands)
	{
		RHICmdList.SubmitCommandsHint();
	}
}

void NiagaraEmitterInstanceBatcher::PostRenderOpaque(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct, FRHIUniformBuffer* SceneTexturesUniformBuffer, bool bAllowGPUParticleUpdate)
{
	LLM_SCOPE(ELLMTag::Niagara);

	if (bAllowGPUParticleUpdate)
	{
		// Setup new readback since if there is no pending request, there is no risk of having invalid data read (offset being allocated after the readback was sent).
		ExecuteAll(RHICmdList, ViewUniformBuffer, !GPUInstanceCounterManager.HasPendingGPUReadback(), ETickStage::PostOpaqueRender);

		FinishDispatches();
	}

	if (!GPUInstanceCounterManager.HasPendingGPUReadback())
	{
		GPUInstanceCounterManager.EnqueueGPUReadback(RHICmdList);
	}
}

bool NiagaraEmitterInstanceBatcher::ShouldTickForStage(const FNiagaraGPUSystemTick& Tick, ETickStage TickStage) const
{
	if (!GNiagaraAllowTickBeforeRender || Tick.bRequiresDistanceFieldData || Tick.bRequiresDepthBuffer)
	{
		return TickStage == ETickStage::PostOpaqueRender;
	}

	if (Tick.bRequiresEarlyViewData)
	{
		return TickStage == ETickStage::PostInitViews;
	}

	FNiagaraShader* ComputeShader = Tick.GetInstanceData()->Context->GPUScript_RT->GetShader();
	if (ComputeShader->ViewUniformBufferParam.IsBound())
	{
		return TickStage == ETickStage::PostOpaqueRender;
	}
	return TickStage == ETickStage::PreInitViews;
}

void NiagaraEmitterInstanceBatcher::ExecuteAll(FRHICommandList &RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bSetReadback, ETickStage TickStage)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUSimTick_RT);

	// This is always called by the renderer so early out if we have no work.
	if (Ticks_RT.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NiagaraEmitterInstanceBatcher_ExecuteAll);

	FMemMark Mark(FMemStack::Get());
	TArray<FOverlappableTicks, TMemStackAllocator<> > SimPasses;
	{
		TArray< FNiagaraComputeExecutionContext* , TMemStackAllocator<> > RelevantContexts;
		TArray< FNiagaraGPUSystemTick* , TMemStackAllocator<> > RelevantTicks;
		for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
		{
			FNiagaraComputeInstanceData* Data = Tick.GetInstanceData();
			FNiagaraComputeExecutionContext* Context = Data->Context;
			// This assumes all emitters fallback to the same FNiagaraShaderScript*.
			FNiagaraShader* ComputeShader = Context->GPUScript_RT->GetShader();
			if (!ComputeShader || !ShouldTickForStage(Tick, TickStage))
			{
				continue;
			}

			Tick.bIsFinalTick = false; // @todo : this is true sometimes, needs investigation
			if (Context->ScratchIndex == INDEX_NONE)
			{
				RelevantContexts.Add(Context);
			}
			// Here scratch index represent the index of the last tick
			Context->ScratchIndex = RelevantTicks.Add(&Tick);
		}

		// Set bIsFinalTick for the last tick of each context and reset the scratch index.
		const int32 ScrachIndexReset = GNiagaraOverlapCompute ? 0 : INDEX_NONE;
		for (FNiagaraComputeExecutionContext* Context : RelevantContexts)
		{
			RelevantTicks[Context->ScratchIndex]->bIsFinalTick = true;
			Context->ScratchIndex = ScrachIndexReset;
		}

		if (GNiagaraOverlapCompute)
		{
			// Transpose now only once the data to get all independent tick per pass
			SimPasses.Reserve(2); // Safe bet!

			for (FNiagaraGPUSystemTick* Tick : RelevantTicks)
			{
				FNiagaraComputeExecutionContext* Context = Tick->GetInstanceData()->Context;
				const int32 ScratchIndex = Context->ScratchIndex;
				check(ScratchIndex != INDEX_NONE);

				if (ScratchIndex >= SimPasses.Num())
				{
					SimPasses.AddDefaulted(SimPasses.Num() - ScratchIndex + 1);
					if (ScratchIndex == 0)
					{
						SimPasses[0].Reserve(RelevantContexts.Num()); // Guarantied!
					}
				}
				SimPasses[ScratchIndex].Add(Tick);
				// Scratch index is now the number of passes for this context.
				if (Tick->bIsFinalTick)
				{
					// Reset to default as it will no longer be used.
					Context->ScratchIndex = INDEX_NONE;
				}
				else
				{
					Context->ScratchIndex += 1;
				}
			}
		}
		else
		{
			// Force dispatches to run individually, this should only be used for debugging as it is highly inefficient on the GPU
			SimPasses.Reserve(RelevantTicks.Num()); // Guarantied!
			for (FNiagaraGPUSystemTick* Tick : RelevantTicks)
			{
				SimPasses.AddDefaulted_GetRef().Add(Tick);
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

		SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUSimulation);
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSimulation);
		DispatchAllOnCompute(SimPass, RHICmdList, ViewUniformBuffer, DestDataBuffers, CurrDataBuffers, DestBufferIntFloat, CurrBufferIntFloat, bSetReadback);
	}
}

void NiagaraEmitterInstanceBatcher::PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate)
{
	LLM_SCOPE(ELLMTag::Niagara);

	SortedParticleCount = 0;
	SimulationsToSort.Reset();

	for (FNiagaraIndicesVertexBuffer& SortedVertexBuffer : SortedVertexBuffers)
	{
		SortedVertexBuffer.UsedIndexCount = 0;
	}

	// Update draw indirect buffer to max possible size.
	if (bAllowGPUParticleUpdate)
	{
		int32 TotalDispatchCount = 0;
		for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
		{
			TotalDispatchCount += (int32)Tick.Count;

			// Cancel any pending readback if the emitter is resetting.
			if (Tick.bNeedsReset)
			{
				FNiagaraComputeInstanceData* Instances = Tick.GetInstanceData();
				for (uint32 InstanceIndex = 0; InstanceIndex < Tick.Count; ++InstanceIndex)
				{
					FNiagaraComputeExecutionContext* Context = Instances[InstanceIndex].Context;
					if (Context)
					{
						GPUInstanceCounterManager.FreeEntry(Context->EmitterInstanceReadback.GPUCountOffset);
					}
				}
			}
		}
		GPUInstanceCounterManager.ResizeBuffers(RHICmdList, FeatureLevel, TotalDispatchCount);

		// Update the instance counts from the GPU readback.
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUReadback_RT);
			const uint32* Counts = GPUInstanceCounterManager.GetGPUReadback();
			if (Counts)
			{
				for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
				{
					FNiagaraComputeInstanceData* Instances = Tick.GetInstanceData();
					for (uint32 InstanceIndex = 0; InstanceIndex < Tick.Count; ++InstanceIndex)
					{
						FNiagaraComputeExecutionContext* Context = Instances[InstanceIndex].Context;
						if (Context && Context->EmitterInstanceReadback.GPUCountOffset != INDEX_NONE)
						{
							check(Context->MainDataSet);
							FNiagaraDataBuffer* CurrentData = Context->MainDataSet->GetCurrentData();
							if (CurrentData)
							{
								const uint32 DeadInstanceCount = Context->EmitterInstanceReadback.CPUCount - Counts[Context->EmitterInstanceReadback.GPUCountOffset];

								// This will communicate the particle counts to the game thread. If DeadInstanceCount equals CurrentData->GetNumInstances() the game thread will know that the emitter has completed.
								if (DeadInstanceCount <= CurrentData->GetNumInstances())
								{
									CurrentData->SetNumInstances(CurrentData->GetNumInstances() - DeadInstanceCount); 
								}
							}

							// Now release the readback since another one will be enqueued in the tick.
							// Also prevents processing the same data again.
							GPUInstanceCounterManager.FreeEntry(Context->EmitterInstanceReadback.GPUCountOffset);
						}
					}
				}
				// Readback is only valid for one frame, so that any newly allocated instance count
				// are guarantied to be in the next valid readback data.
				GPUInstanceCounterManager.ReleaseGPUReadback();
			}
		}

		// @todo REMOVE THIS HACK
		LastFrameThatDrainedData = GFrameNumberRenderThread;

		if (GNiagaraAllowTickBeforeRender)
		{
			ExecuteAll(RHICmdList, nullptr, !GPUInstanceCounterManager.HasPendingGPUReadback(), ETickStage::PreInitViews);
		}
	}
	else
	{
		GPUInstanceCounterManager.ResizeBuffers(RHICmdList, FeatureLevel,  0);
	}
}

void NiagaraEmitterInstanceBatcher::PostInitViews(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bAllowGPUParticleUpdate)
{
	LLM_SCOPE(ELLMTag::Niagara);

	if (bAllowGPUParticleUpdate)
	{
		ExecuteAll(RHICmdList, ViewUniformBuffer, false, ETickStage::PostInitViews);
	}
}

bool NiagaraEmitterInstanceBatcher::UsesGlobalDistanceField() const
{
	for (const FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		if (Tick.bRequiresDistanceFieldData)
		{
			return true;
		}
	}

	return false;
}

bool NiagaraEmitterInstanceBatcher::UsesDepthBuffer() const
{
	for (const FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		if (Tick.bRequiresDepthBuffer)
		{
			return true;
		}
	}

	return false;
}

bool NiagaraEmitterInstanceBatcher::RequiresEarlyViewUniformBuffer() const
{
	for (const FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		if (Tick.bRequiresEarlyViewData)
		{
			return true;
		}
	}

	return false;
}

void NiagaraEmitterInstanceBatcher::PreRender(FRHICommandListImmediate& RHICmdList, const class FGlobalDistanceFieldParameterData* GlobalDistanceFieldParameterData, bool bAllowGPUParticleUpdate)
{
	LLM_SCOPE(ELLMTag::Niagara);

	GlobalDistanceFieldParams = GlobalDistanceFieldParameterData ? *GlobalDistanceFieldParameterData : FGlobalDistanceFieldParameterData();

	// Sort buffer after mesh batches are issued, before tick (which will change the GPU instance count).
	SortGPUParticles(RHICmdList);

	// Update draw indirect args from the simulation results.
	GPUInstanceCounterManager.UpdateDrawIndirectBuffer(RHICmdList, FeatureLevel);

}

void NiagaraEmitterInstanceBatcher::OnDestroy()
{
	FNiagaraWorldManager::OnBatcherDestroyed(this);
	FFXSystemInterface::OnDestroy();
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

		//UE_LOG(LogNiagara, Warning, TEXT("NiagaraEmitterInstanceBatcher::SortGPUParticles:  %0xP"), this);

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
		else // Reset counter since we are not in a shrinking situation anymore.
		{
			NumFramesRequiringShrinking = 0;
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

void NiagaraEmitterInstanceBatcher::ProcessDebugInfo(FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext* Context) const
{
#if WITH_EDITORONLY_DATA
	// This method may be called from one of two places: in the tick or as part of a paused frame looking for the debug info that was submitted previously...
	// Note that PrevData is where we expect the data to be for rendering
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

			int32 NewExistingDataCount =  static_cast<int32*>(Context->GPUDebugDataReadbackCounts->Lock((Context->GPUDebugDataCountOffset + 1) * sizeof(int32)))[Context->GPUDebugDataCountOffset];
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
				Context->GPUDebugDataCountOffset = INDEX_NONE;
			}

			// We've updated the debug info directly, now we need to no longer keep asking and querying because this frame is done!
			Context->DebugInfo.Reset();
		}
	}
#endif // WITH_EDITORONLY_DATA
}

/* Set shader parameters for data interfaces
 */
void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, FNiagaraShader* Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 ShaderStageIndex) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FNiagaraSystemInstanceID& SystemInstance = Tick.SystemInstanceID;

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
			Context.ShaderStageIndex = ShaderStageIndex;
			Context.IsOutputStage = Interface->IsOutputStage(ShaderStageIndex);
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
	const FNiagaraSystemInstanceID& SystemInstance = Tick.SystemInstanceID;

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
void NiagaraEmitterInstanceBatcher::Run(const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData* Instance, uint32 UpdateStartInstance, const uint32 TotalNumInstances, FNiagaraShader* Shader,
	FRHICommandList &RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const FNiagaraGpuSpawnInfo& SpawnInfo, bool bCopyBeforeStart, uint32 DefaultShaderStageIndex, uint32 ShaderStageIndex, FNiagaraDataInterfaceProxy *IterationInterface, bool HasRunParticleStage) const
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;
	if (TotalNumInstances == 0)
	{
#if !UE_BUILD_SHIPPING
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u - StageNumber: %u"),
			Context->GetDebugSimName(),
			TotalNumInstances,
			ShaderStageIndex);
#endif
		return;
	}

	//UE_LOG(LogNiagara, Warning, TEXT("Run"));
	
	const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies = Instance->DataInterfaceProxies;
	const FRHIUniformBufferLayout& CBufferLayout = Context->CBufferLayout;
	check(Instance->CurrentData && Instance->DestinationData);
	FNiagaraDataBuffer& DestinationData = *Instance->DestinationData;
	FNiagaraDataBuffer& CurrentData = *Instance->CurrentData;

	RHICmdList.SetComputeShader(Shader->GetComputeShader());

	// #todo(dmp): clean up this logic for shader stages on first frame
	if (Shader->SimStartParam.IsBound())
	{
		int v = Tick.bNeedsReset ? 1 : 0;
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->SimStartParam.GetBufferIndex(), Shader->SimStartParam.GetBaseIndex(), Shader->SimStartParam.GetNumBytes(), &v);
	}
	

	// set the view uniform buffer param
	if (Shader->ViewUniformBufferParam.IsBound() && ViewUniformBuffer)
	{
		RHICmdList.SetShaderUniformBuffer(Shader->GetComputeShader(), Shader->ViewUniformBufferParam.GetBaseIndex(), ViewUniformBuffer);
	}

	SetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick, ShaderStageIndex);

	// set the shader and data set params 
	//
	CurrentData.SetShaderParams(Shader, RHICmdList, true);
	DestinationData.SetShaderParams(Shader, RHICmdList, false);

	// set the index buffer uav
	//
	if (Shader->InstanceCountsParam.IsBound() && !IterationInterface )
	{
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
		RHICmdList.SetUAVParameter(Shader->GetComputeShader(), Shader->InstanceCountsParam.GetUAVIndex(), GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
		const uint32 ReadOffset = Tick.bNeedsReset ? INDEX_NONE : CurrentData.GetGPUInstanceCountBufferOffset();
		const uint32 WriteOffset = DestinationData.GetGPUInstanceCountBufferOffset();
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->ReadInstanceCountOffsetParam.GetBufferIndex(), Shader->ReadInstanceCountOffsetParam.GetBaseIndex(), Shader->ReadInstanceCountOffsetParam.GetNumBytes(), &ReadOffset);
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->WriteInstanceCountOffsetParam.GetBufferIndex(), Shader->WriteInstanceCountOffsetParam.GetBaseIndex(), Shader->WriteInstanceCountOffsetParam.GetNumBytes(), &WriteOffset);
	}

	// set the execution parameters
	//
	if (Shader->EmitterTickCounterParam.IsBound())
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->EmitterTickCounterParam.GetBufferIndex(), Shader->EmitterTickCounterParam.GetBaseIndex(), Shader->EmitterTickCounterParam.GetNumBytes(), &FNiagaraComputeExecutionContext::TickCounter);
	}

	// set spawn info
	//
	static_assert((sizeof(SpawnInfo.SpawnInfoStartOffsets) % SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) == 0, "sizeof SpawnInfoStartOffsets should be a multiple of SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT");
	static_assert((sizeof(SpawnInfo.SpawnInfoParams) % SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) == 0, "sizeof SpawnInfoParams should be a multiple of SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT");
	SetShaderValueArray(RHICmdList, Shader->GetComputeShader(), Shader->EmitterSpawnInfoOffsetsParam, SpawnInfo.SpawnInfoStartOffsets, NIAGARA_MAX_GPU_SPAWN_INFOS_V4);
	SetShaderValueArray(RHICmdList, Shader->GetComputeShader(), Shader->EmitterSpawnInfoParamsParam, SpawnInfo.SpawnInfoParams, NIAGARA_MAX_GPU_SPAWN_INFOS);

	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->UpdateStartInstanceParam.GetBufferIndex(), Shader->UpdateStartInstanceParam.GetBaseIndex(), Shader->UpdateStartInstanceParam.GetNumBytes(), &UpdateStartInstance);					// 0, except for event handler runs
	int32 InstancesToSpawnThisFrame = Instance->SpawnInfo.SpawnRateInstances + Instance->SpawnInfo.EventSpawnTotal;

	// Only spawn particles on the first stage
	if (HasRunParticleStage)
	{
		InstancesToSpawnThisFrame = 0;
	}

	RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->NumSpawnedInstancesParam.GetBufferIndex(), Shader->NumSpawnedInstancesParam.GetBaseIndex(), Shader->NumSpawnedInstancesParam.GetNumBytes(), &InstancesToSpawnThisFrame);				// number of instances in the spawn run
	
	if (Shader->DefaultShaderStageIndexParam.IsBound())
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->DefaultShaderStageIndexParam.GetBufferIndex(), Shader->DefaultShaderStageIndexParam.GetBaseIndex(), Shader->DefaultShaderStageIndexParam.GetNumBytes(), &DefaultShaderStageIndex);					// 0, except if several stages are defined
	}
	
	if (Shader->ShaderStageIndexParam.IsBound())
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->ShaderStageIndexParam.GetBufferIndex(), Shader->ShaderStageIndexParam.GetBaseIndex(), Shader->ShaderStageIndexParam.GetNumBytes(), &ShaderStageIndex);					// 0, except if several stages are defined
	}
	const int32 DefaultIterationCount = -1;
	if (Shader->IterationInterfaceCount.IsBound())
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->IterationInterfaceCount.GetBufferIndex(), Shader->IterationInterfaceCount.GetBaseIndex(), Shader->IterationInterfaceCount.GetNumBytes(), &DefaultIterationCount);					// 0, except if several stages are defined
	}

	const uint32 ShaderThreadGroupSize = FNiagaraShader::GetGroupSize(ShaderPlatform);
	if (IterationInterface)
	{
		if (TotalNumInstances > ShaderThreadGroupSize)
		{
			RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->IterationInterfaceCount.GetBufferIndex(), Shader->IterationInterfaceCount.GetBaseIndex(), Shader->IterationInterfaceCount.GetNumBytes(), &TotalNumInstances);					// 0, except if several stages are defined
		}
	}

	uint32 NumThreadGroups = 1;
	if (TotalNumInstances > ShaderThreadGroupSize)
	{
		NumThreadGroups = FMath::Min(NIAGARA_MAX_COMPUTE_THREADGROUPS, FMath::DivideAndRoundUp(TotalNumInstances, ShaderThreadGroupSize));
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

	// #todo(dmp): temporary hack -- unbind UAVs if we have a valid iteration DI.  This way, when we are outputting with a different iteration count, we don't
	// mess up particle state
	if (IterationInterface)
	{
		CurrentData.UnsetShaderParams(Shader, RHICmdList);
		DestinationData.UnsetShaderParams(Shader, RHICmdList);
	}

	//UE_LOG(LogNiagara, Log, TEXT("Num Instance : %d | Num Group : %d | Spawned Istance : %d | Start Instance : %d | Num Indices : %d | Stage Index : %d"), 
		//TotalNumInstances, NumThreadGroups, InstancesToSpawnThisFrame, UpdateStartInstance, Context->NumIndicesPerInstance, ShaderStageIndex);

	// Dispatch, if anything needs to be done
	if (TotalNumInstances)
	{
#if !UE_BUILD_SHIPPING
		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u - StageNumber: %u"),
			Context->GetDebugSimName(),
			TotalNumInstances,
			ShaderStageIndex);
#endif
		DispatchComputeShader(RHICmdList, Shader, NumThreadGroups, 1, 1);
	}

	// reset iteration count
	if (IterationInterface)
	{
		RHICmdList.SetShaderParameter(Shader->GetComputeShader(), Shader->IterationInterfaceCount.GetBufferIndex(), Shader->IterationInterfaceCount.GetBaseIndex(), Shader->IterationInterfaceCount.GetNumBytes(), &DefaultIterationCount);					// 0, except if several stages are defined
	}

#if WITH_EDITORONLY_DATA
	// Check to see if we need to queue up a debug dump..
	if (Context->DebugInfo.IsValid())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Queued up!"));

		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackCounts && DestinationData.GetGPUInstanceCountBufferOffset() != INDEX_NONE && ShaderStageIndex == Context->MaxUpdateIterations - 1)
		{
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
			Context->GPUDebugDataReadbackCounts->EnqueueCopy(RHICmdList, GPUInstanceCounterManager.GetInstanceCountBuffer().Buffer);
			Context->GPUDebugDataCountOffset = DestinationData.GetGPUInstanceCountBufferOffset();
		}
	}
#endif // WITH_EDITORONLY_DATA

	// Unset UAV parameters and transition resources (TODO: resource transition should be moved to the renderer)
	// 
	UnsetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick);
	CurrentData.UnsetShaderParams(Shader, RHICmdList);
	DestinationData.UnsetShaderParams(Shader, RHICmdList);
	Shader->InstanceCountsParam.UnsetUAV(RHICmdList, Shader->GetComputeShader());
}
