// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraScriptExecutionContext.h"
#include "RHI.h"
#include "RHIGPUReadback.h"
#include "NiagaraStats.h"
#include "NiagaraShader.h"
#include "NiagaraSortingGPU.h"
#include "NiagaraWorldManager.h"
#include "NiagaraShaderParticleID.h"
#include "NiagaraRenderer.h"
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
DECLARE_GPU_STAT_NAMED(NiagaraGPUClearIDTables, TEXT("NiagaraGPU Clear ID Tables"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUComputeFreeIDs, TEXT("Niagara GPU Compute All Free IDs"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUComputeFreeIDsEmitter, TEXT("Niagara GPU Compute Emitter Free IDs"));
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

// @todo REMOVE THIS HACK
int32 GNiagaraGpuMaxQueuedRenderFrames = 10;
static FAutoConsoleVariableRef CVarNiagaraGpuMaxQueuedRenderFrames(
	TEXT("fx.NiagaraGpuMaxQueuedRenderFrames"),
	GNiagaraGpuMaxQueuedRenderFrames,
	TEXT("Number of frames we all to pass before we start to discard GPU ticks.\n"),
	ECVF_Default
);

const FName NiagaraEmitterInstanceBatcher::Name(TEXT("NiagaraEmitterInstanceBatcher"));

FFXSystemInterface* NiagaraEmitterInstanceBatcher::GetInterface(const FName& InName)
{
	return InName == Name ? this : nullptr;
}

NiagaraEmitterInstanceBatcher::NiagaraEmitterInstanceBatcher(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, FGPUSortManager* InGPUSortManager)
	: FeatureLevel(InFeatureLevel)
	, ShaderPlatform(InShaderPlatform)
	, GPUSortManager(InGPUSortManager)
	, GlobalCBufferLayout(TEXT("Niagara GPU Global CBuffer"))
	, SystemCBufferLayout(TEXT("Niagara GPU System CBuffer"))
	, OwnerCBufferLayout(TEXT("Niagara GPU Owner CBuffer"))
	, EmitterCBufferLayout(TEXT("Niagara GPU Emitter CBuffer"))
	// @todo REMOVE THIS HACK
	, LastFrameThatDrainedData(GFrameNumberRenderThread)
	, NumAllocatedFreeIDListSizes(0)
	, bFreeIDListSizesBufferCleared(false)
{
	// Register the batcher callback in the GPUSortManager. 
	// The callback is used to generate the initial keys and values for the GPU sort tasks, 
	// the values being the sorted particle indices used by the Niagara renderers.
	// The registration also involves defining the list of flags possibly used in GPUSortManager::AddTask()
	if (GPUSortManager)
	{
		GPUSortManager->Register(FGPUSortKeyGenDelegate::CreateLambda([this](FRHICommandListImmediate& RHICmdList, int32 BatchId, int32 NumElementsInBatch, EGPUSortFlags Flags, FRHIUnorderedAccessView* KeysUAV, FRHIUnorderedAccessView* ValuesUAV)
		{ 
			GenerateSortKeys(RHICmdList, BatchId, NumElementsInBatch, Flags, KeysUAV, ValuesUAV);
		}), 
		EGPUSortFlags::AnyKeyPrecision | EGPUSortFlags::KeyGenAfterPreRender | EGPUSortFlags::AnySortLocation | EGPUSortFlags::ValuesAsInt32,
		Name);

		if (FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
		{
			// Because of culled indirect draw args, we have to update the draw indirect buffer after the sort key generation
			GPUSortManager->PostPreRenderEvent.AddLambda(
				[this](FRHICommandListImmediate& RHICmdList)
				{
					GPUInstanceCounterManager.UpdateDrawIndirectBuffer(RHICmdList, FeatureLevel);
				}
			);
		}
	}

	GlobalCBufferLayout.ConstantBufferSize = sizeof(FNiagaraGlobalParameters);
	GlobalCBufferLayout.ComputeHash();

	SystemCBufferLayout.ConstantBufferSize = sizeof(FNiagaraSystemParameters);
	SystemCBufferLayout.ComputeHash();

	OwnerCBufferLayout.ConstantBufferSize = sizeof(FNiagaraOwnerParameters);
	OwnerCBufferLayout.ComputeHash();

	EmitterCBufferLayout.ConstantBufferSize = sizeof(FNiagaraEmitterParameters);
	EmitterCBufferLayout.ComputeHash();
}

NiagaraEmitterInstanceBatcher::~NiagaraEmitterInstanceBatcher()
{
	FinishDispatches();
}

void NiagaraEmitterInstanceBatcher::InstanceDeallocated_RenderThread(const FNiagaraSystemInstanceID InstanceID)
{
	int iTick = 0;
	while ( iTick < Ticks_RT.Num() )
	{
		FNiagaraGPUSystemTick& Tick = Ticks_RT[iTick];
		if (Tick.SystemInstanceID == InstanceID)
		{
			//-OPT: Since we can't RemoveAtSwap (due to ordering issues) if may be better to not remove and flag as dead
			Tick.Destroy();
			Ticks_RT.RemoveAt(iTick);
		}
		else
		{
			++iTick;
		}
	}
}

void NiagaraEmitterInstanceBatcher::BuildConstantBuffers(FNiagaraGPUSystemTick& Tick)
{
	if (!Tick.Count)
	{
		return;
	}

	FNiagaraComputeInstanceData* EmittersData = Tick.GetInstanceData();

	// first go through and figure out if we need to support interpolated spawning
	bool HasInterpolationParameters = false;
	bool HasMultipleStages = false;
	for (uint32 CountIt = 0; CountIt < Tick.Count; ++CountIt)
	{
		HasInterpolationParameters = HasInterpolationParameters || EmittersData[CountIt].Context->HasInterpolationParameters;
		HasMultipleStages = HasMultipleStages || EmittersData[CountIt].bUsesOldShaderStages || EmittersData[CountIt].bUsesSimStages;
	}

	int32 BoundParameterCounts[FNiagaraGPUSystemTick::UBT_NumTypes][2];
	for (int32 i = 0; i < FNiagaraGPUSystemTick::UBT_NumTypes; ++i)
	{
		for (int32 j = 0; j < 2; ++j)
		{
			BoundParameterCounts[i][j] = 0;
		}
	}

	for (uint32 CountIt = 0; CountIt < Tick.Count; ++CountIt)
	{
		FNiagaraComputeInstanceData& EmitterData = EmittersData[CountIt];

		const FNiagaraShaderRef& Shader = EmitterData.Context->GPUScript_RT->GetShader();
		const int32 HasExternalConstants = EmitterData.Context->ExternalCBufferLayout.ConstantBufferSize > 0 ? 1 : 0;

		for (int32 InterpIt = 0; InterpIt < (HasInterpolationParameters ? 2 : 1); ++InterpIt)
		{
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Global][InterpIt] += Shader->GlobalConstantBufferParam[InterpIt].IsBound() ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_System][InterpIt] += Shader->SystemConstantBufferParam[InterpIt].IsBound() ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Owner][InterpIt] += Shader->OwnerConstantBufferParam[InterpIt].IsBound() ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Emitter][InterpIt] += Shader->EmitterConstantBufferParam[InterpIt].IsBound() ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_External][InterpIt] += Shader->ExternalConstantBufferParam[InterpIt].IsBound() ? 1 : 0;
		}
	}

	const int32 InterpScale = HasInterpolationParameters ? 2 : 1;
	const int32 BufferCount = InterpScale * (FNiagaraGPUSystemTick::UBT_NumSystemTypes + FNiagaraGPUSystemTick::UBT_NumInstanceTypes * Tick.Count);

	Tick.UniformBuffers.Empty(BufferCount);

	const FRHIUniformBufferLayout* SystemLayouts[FNiagaraGPUSystemTick::UBT_NumSystemTypes] =
	{
		&GlobalCBufferLayout,
		&SystemCBufferLayout,
		&OwnerCBufferLayout
	};

	for (int32 InterpIt = 0; InterpIt < InterpScale; ++InterpIt)
	{
		for (int32 SystemTypeIt = FNiagaraGPUSystemTick::UBT_FirstSystemType; SystemTypeIt < FNiagaraGPUSystemTick::UBT_NumSystemTypes; ++SystemTypeIt)
		{
			FUniformBufferRHIRef BufferRef;

			if (BoundParameterCounts[SystemTypeIt][InterpIt])
			{
				BufferRef = RHICreateUniformBuffer(
					Tick.GetUniformBufferSource((FNiagaraGPUSystemTick::EUniformBufferType) SystemTypeIt, nullptr, !InterpIt),
					*SystemLayouts[SystemTypeIt],
					((BoundParameterCounts[SystemTypeIt][InterpIt] > 1) || HasMultipleStages)
						? EUniformBufferUsage::UniformBuffer_SingleFrame
						: EUniformBufferUsage::UniformBuffer_SingleDraw);
			}

			Tick.UniformBuffers.Add(BufferRef);
		}

		for (int32 InstanceTypeIt = FNiagaraGPUSystemTick::UBT_FirstInstanceType; InstanceTypeIt < FNiagaraGPUSystemTick::UBT_NumTypes; ++InstanceTypeIt)
		{
			for (uint32 InstanceIt = 0; InstanceIt < Tick.Count; ++InstanceIt)
			{
				FNiagaraComputeInstanceData& EmitterData = EmittersData[InstanceIt];

				FUniformBufferRHIRef BufferRef;

				if (BoundParameterCounts[InstanceTypeIt][InterpIt])
				{
					BufferRef = RHICreateUniformBuffer(
						Tick.GetUniformBufferSource((FNiagaraGPUSystemTick::EUniformBufferType) InstanceTypeIt, &EmitterData, !InterpIt),
						InstanceTypeIt == FNiagaraGPUSystemTick::UBT_Emitter
							? EmitterCBufferLayout
							: EmitterData.Context->ExternalCBufferLayout,
						((BoundParameterCounts[InstanceTypeIt][InterpIt] > 1) || HasMultipleStages)
							? EUniformBufferUsage::UniformBuffer_SingleFrame
							: EUniformBufferUsage::UniformBuffer_SingleDraw);
				}

				Tick.UniformBuffers.Add(BufferRef);
			}
		}

	}
}

void NiagaraEmitterInstanceBatcher::GiveSystemTick_RenderThread(FNiagaraGPUSystemTick& Tick)
{
	check(IsInRenderingThread());

	if (!FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		return;
	}


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
	FNiagaraGPUSystemTick& AddedTick = Ticks_RT.Add_GetRef(Tick);

	BuildConstantBuffers(AddedTick);
}

void NiagaraEmitterInstanceBatcher::ReleaseInstanceCounts_RenderThread(FNiagaraComputeExecutionContext* ExecContext, FNiagaraDataSet* DataSet)
{
	LLM_SCOPE(ELLMTag::Niagara);

	if ( ExecContext != nullptr )
	{
		GPUInstanceCounterManager.FreeEntry(ExecContext->EmitterInstanceReadback.GPUCountOffset);
	}
	if ( DataSet != nullptr )
	{
		DataSet->ReleaseGPUInstanceCounts(GPUInstanceCounterManager);
	}
}

void NiagaraEmitterInstanceBatcher::FinishDispatches()
{
	ReleaseTicks();
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

bool NiagaraEmitterInstanceBatcher::UseOverlapCompute()
{
	return !IsMobilePlatform(ShaderPlatform) && GNiagaraOverlapCompute;
}

bool NiagaraEmitterInstanceBatcher::ResetDataInterfaces(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, const FNiagaraShaderRef& ComputeShader ) const
{
	bool ValidSpawnStage = true;
	FNiagaraComputeExecutionContext* Context = Instance->Context;

	// Reset all rw data interface data
	if (Tick.bNeedsReset)
	{
		uint32 InterfaceIndex = 0;
		for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
		{
			const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
			if (DIParam.Parameters.IsValid())
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

FNiagaraDataInterfaceProxy* NiagaraEmitterInstanceBatcher::FindIterationInterface( FNiagaraComputeInstanceData *Instance, const uint32 SimulationStageIndex) const
{
	// Determine if the iteration is outputting to a custom data size
	return Instance->FindIterationInterface(SimulationStageIndex);
}

void NiagaraEmitterInstanceBatcher::PreStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, const FNiagaraShaderRef& ComputeShader, const uint32 SimulationStageIndex) const
{
	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			FNiagaraDataInterfaceSetArgs TmpContext;
			TmpContext.Shader = ComputeShader;
			TmpContext.DataInterface = Interface;
			TmpContext.SystemInstance = Tick.SystemInstanceID;
			TmpContext.Batcher = this;
			TmpContext.SimulationStageIndex = SimulationStageIndex;
			TmpContext.IsOutputStage = Instance->IsOutputStage(Interface, SimulationStageIndex);
			TmpContext.IsIterationStage = Instance->IsIterationStage(Interface, SimulationStageIndex);
			Interface->PreStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::PostStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData *Instance, FRHICommandList &RHICmdList, const FNiagaraShaderRef& ComputeShader, const uint32 SimulationStageIndex) const
{
	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			FNiagaraDataInterfaceSetArgs TmpContext;
			TmpContext.Shader = ComputeShader;
			TmpContext.DataInterface = Interface;
			TmpContext.SystemInstance = Tick.SystemInstanceID;
			TmpContext.Batcher = this;
			TmpContext.SimulationStageIndex = SimulationStageIndex;
			TmpContext.IsOutputStage = Instance->IsOutputStage(Interface, SimulationStageIndex);
			TmpContext.IsIterationStage = Instance->IsIterationStage(Interface, SimulationStageIndex);
			Interface->PostStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::TransitionBuffers(FRHICommandList& RHICmdList, const FNiagaraShaderRef& ComputeShader, FNiagaraComputeInstanceData* Instance, uint32 SimulationStageIndex, const FNiagaraDataBuffer*& LastSource, bool& bFreeIDTableTransitioned)
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;
	const bool bRequiresPersistentIDs = Context->MainDataSet->RequiresPersistentIDs();
	FNiagaraSimStageData& SimStageData = Instance->SimStageData[SimulationStageIndex];

	FNiagaraBufferArray ReadBuffers, WriteBuffers;

	if (bRequiresPersistentIDs && !bFreeIDTableTransitioned)
	{
		// There's one free ID buffer at the emitter level, so we only need to make it writable once.
		ReadBuffers.Add(Context->MainDataSet->GetGPUFreeIDs().UAV);
		bFreeIDTableTransitioned = true;
	}

	if (LastSource != SimStageData.Source)
	{
		// If we're reading from a different buffer set than the previous stage, make the inputs readable.
		LastSource = SimStageData.Source;
		if (ComputeShader->FloatInputBufferParam.IsBound())
		{
			ReadBuffers.Add(SimStageData.Source->GetGPUBufferFloat().UAV);
		}
		if (ComputeShader->HalfInputBufferParam.IsBound())
		{
			ReadBuffers.Add(SimStageData.Source->GetGPUBufferHalf().UAV);
		}
		if (ComputeShader->IntInputBufferParam.IsBound())
		{
			ReadBuffers.Add(SimStageData.Source->GetGPUBufferInt().UAV);
		}
	}

	// Always transition output buffers.
	if (ComputeShader->FloatOutputBufferParam.IsBound())
	{
		WriteBuffers.Add(SimStageData.Destination->GetGPUBufferFloat().UAV);
	}
	if (ComputeShader->HalfOutputBufferParam.IsBound())
	{
		WriteBuffers.Add(SimStageData.Destination->GetGPUBufferHalf().UAV);
	}
	if (ComputeShader->IntOutputBufferParam.IsBound())
	{
		WriteBuffers.Add(SimStageData.Destination->GetGPUBufferInt().UAV);
	}
	if (bRequiresPersistentIDs)
	{
		// TODO: clear the ID to Index table for stages > 0. We may be able to
		// skip this in some cases.
		WriteBuffers.Add(SimStageData.Destination->GetGPUIDToIndexTable().UAV);
	}

	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ReadBuffers.GetData(), ReadBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, WriteBuffers.GetData(), WriteBuffers.Num());
}

void NiagaraEmitterInstanceBatcher::DispatchMultipleStages(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData* Instance, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const FNiagaraShaderRef& ComputeShader)
{
	if (!ResetDataInterfaces(Tick, Instance, RHICmdList, ComputeShader))
	{
		return;
	}

	FNiagaraComputeExecutionContext* Context = Instance->Context;
	const FNiagaraDataBuffer* LastSource = nullptr;
	bool bFreeIDTableTransitioned = false;

	if (Tick.NumInstancesWithSimStages > 0)
	{
		bool HasRunParticleStage = false;
		
		const uint32 NumStages = Instance->Context->MaxUpdateIterations;
		const uint32 DefaultSimulationStageIndex = Instance->Context->DefaultSimulationStageIndex;

		for (uint32 SimulationStageIndex = 0; SimulationStageIndex < NumStages; ++SimulationStageIndex)
		{
			// Determine if the iteration is outputting to a custom data size
			FNiagaraDataInterfaceProxy *IterationInterface = Instance->SimStageData[SimulationStageIndex].AlternateIterationSource;

			//UE_LOG(LogNiagara, Log, TEXT("Starting sim stage %d Iteration %p. %s %s"), SimulationStageIndex, IterationInterface, Tick.bNeedsReset? TEXT("bNeedsReset") : TEXT("!bNeedsReset"), Context->GetDebugSimName());

			if (IterationInterface && Context->SpawnStages.Num() > 0 &&
				((Tick.bNeedsReset && !Context->SpawnStages.Contains(SimulationStageIndex)) ||
				(!Tick.bNeedsReset && Context->SpawnStages.Contains(SimulationStageIndex))))
			{
				//UE_LOG(LogNiagara, Log, TEXT("Skipping sim stage %d  because iteration interface and spawn stage not on a reset. %s"), SimulationStageIndex, Context->GetDebugSimName());
				continue;
			}

			PreStageInterface(Tick, Instance, RHICmdList, ComputeShader, SimulationStageIndex);

			TransitionBuffers(RHICmdList, ComputeShader, Instance, SimulationStageIndex, LastSource, bFreeIDTableTransitioned);

			if (!IterationInterface)
			{
				Run(Tick, Instance, 0, Instance->SimStageData[SimulationStageIndex].Destination->GetNumInstances(), ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo, false, DefaultSimulationStageIndex, SimulationStageIndex,  nullptr, HasRunParticleStage);
				HasRunParticleStage = true;
			}
			else
			{
				// run with correct number of instances.  This will make curr data junk or empty
				Run(Tick, Instance, 0, IterationInterface->ElementCount, ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo, false, DefaultSimulationStageIndex, SimulationStageIndex, IterationInterface);
			}

			PostStageInterface(Tick, Instance, RHICmdList, ComputeShader, SimulationStageIndex);
		}
	}
	else
	{
		// run shader, sim and spawn in a single dispatch
		check(Instance->SimStageData.Num() > 0);
		TransitionBuffers(RHICmdList, ComputeShader, Instance, 0, LastSource, bFreeIDTableTransitioned);
		Run(Tick, Instance, 0, Instance->SimStageData[0].Destination->GetNumInstances(), ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo);
	}
}

void NiagaraEmitterInstanceBatcher::ResizeBuffersAndGatherResources(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FNiagaraBufferArray& OutputGraphicsBuffers, FEmitterInstanceList& InstancesWithPersistentIDs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUDispatchSetup_RT);

	//UE_LOG(LogNiagara, Warning, TEXT("NiagaraEmitterInstanceBatcher::ResizeBuffersAndGatherResources:  %0xP"), this);
	for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
	{
		//UE_LOG(LogNiagara, Warning, TEXT("NiagaraEmitterInstanceBatcher::ResizeBuffersAndGatherResources Tick:  %p  Count: %d"), Tick, Tick->Count);

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

			FNiagaraShaderRef Shader = Context->GPUScript_RT->GetShader();
			if ( Shader.IsNull() )
			{
				continue;
			}

			const bool bRequiresPersistentIDs = Context->MainDataSet->RequiresPersistentIDs();

			check(Instance.SimStageData.Num() == Context->MaxUpdateIterations);

			//The buffer containing current simulation state.
			Instance.SimStageData[0].Source = Context->MainDataSet->GetCurrentData();
			//The buffer we're going to write simulation results to.
			Instance.SimStageData[0].Destination = &Context->MainDataSet->BeginSimulate();

			check(Instance.SimStageData[0].Source && Instance.SimStageData[0].Destination);
			FNiagaraDataBuffer* CurrentData = Instance.SimStageData[0].Source;
			FNiagaraDataBuffer* DestinationData = Instance.SimStageData[0].Destination;

			const uint32 MaxInstanceCount = Context->MainDataSet->GetMaxInstanceCount();
			const uint32 PrevNumInstances = bNeedsReset ? 0 : CurrentData->GetNumInstances();
			check(PrevNumInstances <= MaxInstanceCount && Context->ScratchMaxInstances <= MaxInstanceCount);

			// At this point we can finally clamp the number of instances we're trying to allocate to the max instance count. We don't need to adjust the
			// information inside Instance.SpawnInfo, because that just stores the start indices for each spawner; they're still correct if we spawn
			// fewer particles than expected, it just means that some spawners will not be used.
			const uint32 NewNumInstances = FMath::Min(Instance.SpawnInfo.SpawnRateInstances + Instance.SpawnInfo.EventSpawnTotal + PrevNumInstances, MaxInstanceCount);
			const uint32 AdjustedSpawnCount = (uint32)FMath::Max<int32>((int32)NewNumInstances - PrevNumInstances, 0);

			// We must assume all particles survive when allocating here. If this is not true, the read back in ResolveDatasetWrites will shrink the buffers.
			const uint32 RequiredInstances = FMath::Max(PrevNumInstances, NewNumInstances);

			// We need an extra scratch instance. The code which computes the instance count cap knows about this, so it ensures that we stay within
			// the buffer limit including this instance.
			const uint32 AllocatedInstances = FMath::Max(RequiredInstances, Context->ScratchMaxInstances) + 1;

			if (bRequiresPersistentIDs)
			{
				Context->MainDataSet->AllocateGPUFreeIDs(AllocatedInstances, RHICmdList, FeatureLevel, Context->GetDebugSimName());
				InstancesWithPersistentIDs.Add(&Instance);
			}

			DestinationData->AllocateGPU(AllocatedInstances, GPUInstanceCounterManager, RHICmdList, FeatureLevel, Context->GetDebugSimName());
			DestinationData->SetNumInstances(RequiredInstances);
			DestinationData->SetNumSpawnedInstances(AdjustedSpawnCount);

			Instance.SimStageData[0].SourceCountOffset = Instance.SimStageData[0].Source->GetGPUInstanceCountBufferOffset();
			if (Instance.SimStageData[0].SourceCountOffset == INDEX_NONE) // It is possible that this has been queued for readback, taking ownership of the data. Use that instead.
			{
				Instance.SimStageData[0].SourceCountOffset = Context->EmitterInstanceReadback.GPUCountOffset;
			}
			Instance.SimStageData[0].DestinationCountOffset = Instance.SimStageData[0].Destination->GetGPUInstanceCountBufferOffset();

			//UE_LOG(LogScript, Warning, TEXT("ResizeBuffersAndGatherResources [%d][%d] Run ReqInst: %d Cur: %p Dest: %p "), Index, 0, RequiredInstances, Instance.SimStageData[0].Source, Instance.SimStageData[0].Destination);

			Context->MainDataSet->EndSimulate();

			// Go ahead and reserve the readback data...
			//uint32 ComputeCountOffsetOverride = INDEX_NONE;
			if (!GPUInstanceCounterManager.HasPendingGPUReadback() && Tick->bIsFinalTick)
			{
				// Now that the current data is not required anymore, stage it for readback.
				if (CurrentData->GetNumInstances() && Context->EmitterInstanceReadback.GPUCountOffset == INDEX_NONE && CurrentData->GetGPUInstanceCountBufferOffset() != INDEX_NONE)
				{
					// Transfer the GPU instance counter ownership to the context. Note that a readback request will be performed later in the tick update, unless there's already a pending readback.
					Context->EmitterInstanceReadback.GPUCountOffset = CurrentData->GetGPUInstanceCountBufferOffset();
					Context->EmitterInstanceReadback.CPUCount = CurrentData->GetNumInstances();
					CurrentData->ClearGPUInstanceCountBufferOffset();

					//UE_LOG(LogNiagara, Log, TEXT("EmitterInstanceReadback.CPUCount dispatch %d  Offset: %d"), Context->EmitterInstanceReadback.CPUCount, Context->EmitterInstanceReadback.GPUCountOffset);
				}
			}
			
			uint32 NumBufferIterations = 1;
			if (Tick->NumInstancesWithSimStages > 0)
			{

				bool HasRunParticleStage = false;

				const uint32 NumStages = Instance.Context->MaxUpdateIterations;
				if (NumStages > 1)
				{
					HasRunParticleStage = true;
					for (uint32 SimulationStageIndex = 0; SimulationStageIndex < NumStages; SimulationStageIndex++)
					{
						if (SimulationStageIndex != 0)
						{
							Instance.SimStageData[SimulationStageIndex].Source = Instance.SimStageData[SimulationStageIndex - 1].Source;
							Instance.SimStageData[SimulationStageIndex].Destination = Instance.SimStageData[SimulationStageIndex - 1].Destination;

							Instance.SimStageData[SimulationStageIndex].SourceCountOffset = Instance.SimStageData[SimulationStageIndex - 1].SourceCountOffset;
							Instance.SimStageData[SimulationStageIndex].DestinationCountOffset = Instance.SimStageData[SimulationStageIndex - 1].DestinationCountOffset;
						}

						// Determine if the iteration is outputting to a custom data size
						FNiagaraDataInterfaceProxy* IterationInterface = FindIterationInterface(&Instance, SimulationStageIndex);

						Instance.SimStageData[SimulationStageIndex].AlternateIterationSource = IterationInterface;

						if (IterationInterface && Context->SpawnStages.Num() > 0 &&
							((Tick->bNeedsReset && !Context->SpawnStages.Contains(SimulationStageIndex)) ||
							(!Tick->bNeedsReset && Context->SpawnStages.Contains(SimulationStageIndex))))
						{
							continue;
						}

						if (!IterationInterface && SimulationStageIndex != 0)
						{
							// Go ahead and grab the write buffer, which may be too small, so make sure to resize it.
							Instance.SimStageData[SimulationStageIndex].Source = Context->MainDataSet->GetCurrentData();
							DestinationData = &Context->MainDataSet->BeginSimulate(false);
							Instance.SimStageData[SimulationStageIndex].Destination = DestinationData;
							DestinationData->AllocateGPU(AllocatedInstances, GPUInstanceCounterManager, RHICmdList, FeatureLevel, Context->GetDebugSimName());
							DestinationData->SetNumInstances(RequiredInstances);
							Instance.SimStageData[SimulationStageIndex].SourceCountOffset = Instance.SimStageData[SimulationStageIndex].Source->GetGPUInstanceCountBufferOffset();
							Instance.SimStageData[SimulationStageIndex].DestinationCountOffset = Instance.SimStageData[SimulationStageIndex].Destination->GetGPUInstanceCountBufferOffset();
							
							//UE_LOG(LogScript, Warning, TEXT("ResizeBuffersAndGatherResources [%d][%d] Run  ReqInst: %d Cur: %p Dest: %p "), Index, SimulationStageIndex, RequiredInstances, Instance.SimStageData[SimulationStageIndex].Source, Instance.SimStageData[SimulationStageIndex].Destination);

							// We don't actually write we just map out the buffers here. This toggles src and dest...
							Context->MainDataSet->EndSimulate();
						}


					}
				}
			}

			CurrentData = Context->MainDataSet->GetCurrentData();
			if (bIsFinalTick)
			{
				//UE_LOG(LogScript, Warning, TEXT("ResizeBuffersAndGatherResources [%d] DataSetToRender %p "),Index, CurrentData);

				Context->SetDataToRender(CurrentData);
				OutputGraphicsBuffers.Add(CurrentData->GetGPUBufferFloat().UAV);
				OutputGraphicsBuffers.Add(CurrentData->GetGPUBufferHalf().UAV);
				OutputGraphicsBuffers.Add(CurrentData->GetGPUBufferInt().UAV);
			}
		}
	}

	uint32 NumInstancesWithPersistentIDs = (uint32)InstancesWithPersistentIDs.Num();
	if (NumInstancesWithPersistentIDs > 0)
	{
		// These buffers will be needed by the simulation dispatches which come immediately after, so there will be a stall, but
		// moving this step to a different place is difficult, and the stall is not large, so we'll live with it for now.
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUClearIDTables);
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUClearIDTables);

		FNiagaraBufferArray IDToIndexTables;
		IDToIndexTables.SetNum(NumInstancesWithPersistentIDs);
		for (uint32 i = 0; i < NumInstancesWithPersistentIDs; ++i)
		{
			FNiagaraComputeInstanceData* Instance = InstancesWithPersistentIDs[i];
			IDToIndexTables[i] = Instance->SimStageData[0].Destination->GetGPUIDToIndexTable().UAV;
		}
		// TODO: is it sufficient to do a CS cache flush before all this and get rid of these explicit barriers?
		RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, IDToIndexTables.GetData(), IDToIndexTables.Num());

		for (uint32 i = 0; i < NumInstancesWithPersistentIDs; ++i)
		{
			FNiagaraComputeInstanceData* Instance = InstancesWithPersistentIDs[i];
			SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUComputeClearIDToIndexBuffer, TEXT("Clear ID To Index Table - %s"), Instance->Context->GetDebugSimName());
			NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, Instance->SimStageData[0].Destination->GetGPUIDToIndexTable(), INDEX_NONE);
		}
	}
}

void NiagaraEmitterInstanceBatcher::DispatchAllOnCompute(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer)
{
	FRHICommandListImmediate& RHICmdListImmediate = FRHICommandListExecutor::GetImmediateCommandList();

	//UE_LOG(LogNiagara, Warning, TEXT("NiagaraEmitterInstanceBatcher::DispatchAllOnCompute:  %0xP"), this);

#if WITH_EDITORONLY_DATA
	{
		for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
		{
			uint32 DispatchCount = Tick->Count;
			FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
			for (uint32 Index = 0; Index < DispatchCount; Index++)
			{
				FNiagaraComputeInstanceData& Instance = Instances[Index];
				FNiagaraComputeExecutionContext* Context = Instance.Context;
				if (Context && Context->GPUScript_RT->GetShader().IsValid())
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

	for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
	{
		uint32 DispatchCount = Tick->Count;
		FNiagaraComputeInstanceData* Instances = Tick->GetInstanceData();
		for (uint32 Index = 0; Index < DispatchCount; Index++)
		{
			FNiagaraComputeInstanceData& Instance = Instances[Index];
			FNiagaraComputeExecutionContext* Context = Instance.Context;
			if (Context && Context->GPUScript_RT->GetShader().IsValid())
			{
				FNiagaraComputeExecutionContext::TickCounter++;

				// run shader, sim and spawn in a single dispatch
				DispatchMultipleStages(*Tick, &Instance, RHICmdList, ViewUniformBuffer, Context->GPUScript_RT->GetShader());
			}
		}
	}
}

void NiagaraEmitterInstanceBatcher::PostRenderOpaque(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct, FRHIUniformBuffer* SceneTexturesUniformBuffer, bool bAllowGPUParticleUpdate)
{
	if (!FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		return;
	}

	LLM_SCOPE(ELLMTag::Niagara);

	if (bAllowGPUParticleUpdate)
	{
		// Setup new readback since if there is no pending request, there is no risk of having invalid data read (offset being allocated after the readback was sent).
		ExecuteAll(RHICmdList, ViewUniformBuffer, ETickStage::PostOpaqueRender);

		RHICmdList.BeginUAVOverlap();
		UpdateFreeIDBuffers(RHICmdList, DeferredIDBufferUpdates);
		RHICmdList.EndUAVOverlap();

		DeferredIDBufferUpdates.SetNum(0, false);

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

	FNiagaraShaderRef ComputeShader = Tick.GetInstanceData()->Context->GPUScript_RT->GetShader();
	if (ComputeShader->ViewUniformBufferParam.IsBound())
	{
		return TickStage == ETickStage::PostOpaqueRender;
	}
	return TickStage == ETickStage::PreInitViews;
}

void NiagaraEmitterInstanceBatcher::ResizeFreeIDsListSizesBuffer(uint32 NumInstances)
{
	if (NumInstances <= NumAllocatedFreeIDListSizes)
	{
		return;
	}

	constexpr uint32 ALLOC_CHUNK_SIZE = 128;
	NumAllocatedFreeIDListSizes = Align(NumInstances, ALLOC_CHUNK_SIZE);
	if (FreeIDListSizesBuffer.Buffer)
	{
		FreeIDListSizesBuffer.Release();
	}
	FreeIDListSizesBuffer.Initialize(sizeof(uint32), NumAllocatedFreeIDListSizes, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("NiagaraFreeIDListSizes"));
	bFreeIDListSizesBufferCleared = false;
}

void NiagaraEmitterInstanceBatcher::ClearFreeIDsListSizesBuffer(FRHICommandList& RHICmdList)
{
	if (bFreeIDListSizesBufferCleared)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUComputeClearFreeIDListSizes);
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, FreeIDListSizesBuffer.UAV);
	NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, FreeIDListSizesBuffer, 0);
	bFreeIDListSizesBufferCleared = true;
}

void NiagaraEmitterInstanceBatcher::UpdateFreeIDBuffers(FRHICommandList& RHICmdList, FEmitterInstanceList& Instances)
{
	if (Instances.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUComputeFreeIDs);
	SCOPED_GPU_STAT(RHICmdList, NiagaraGPUComputeFreeIDs);

	FNiagaraBufferArray ReadBuffers, WriteBuffers;
	for(FNiagaraComputeInstanceData* Instance : Instances)
	{
		// TODO: this is incorrect if we have simulation stages which can kill particles.
		ReadBuffers.Add(Instance->SimStageData[0].Destination->GetGPUIDToIndexTable().UAV);
		WriteBuffers.Add(Instance->Context->MainDataSet->GetGPUFreeIDs().UAV);
	}

	RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, FreeIDListSizesBuffer.UAV);
	RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, ReadBuffers.GetData(), ReadBuffers.Num());
	RHICmdList.TransitionResources(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, WriteBuffers.GetData(), WriteBuffers.Num());

	check((uint32)Instances.Num() <= NumAllocatedFreeIDListSizes);

	for (uint32 InstanceIdx = 0; InstanceIdx < (uint32)Instances.Num(); ++InstanceIdx)
	{
		FNiagaraComputeInstanceData* Instance = Instances[InstanceIdx];
		FNiagaraDataSet* MainDataSet = Instance->Context->MainDataSet;
		FNiagaraDataBuffer* DestinationData = Instance->SimStageData[0].Destination;

		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUComputeFreeIDsEmitter, TEXT("Update Free ID Buffer - %s"), Instance->Context->GetDebugSimName());
		NiagaraComputeGPUFreeIDs(RHICmdList, FeatureLevel, MainDataSet->GetGPUNumAllocatedIDs(), DestinationData->GetGPUIDToIndexTable().SRV, MainDataSet->GetGPUFreeIDs(), FreeIDListSizesBuffer, InstanceIdx);
	}

	bFreeIDListSizesBufferCleared = false;
}

void NiagaraEmitterInstanceBatcher::ExecuteAll(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, ETickStage TickStage)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUSimTick_RT);

	// This is always called by the renderer so early out if we have no work.
	if (Ticks_RT.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraEmitterInstanceBatcher_ExecuteAll, TEXT("NiagaraEmitterInstanceBatcher_ExecuteAll - TickStage(%d)"), TickStage);

	FMemMark Mark(FMemStack::Get());
	TArray<FOverlappableTicks, TMemStackAllocator<> > SimPasses;
	{
		TArray< FNiagaraComputeExecutionContext* , TMemStackAllocator<> > RelevantContexts;
		TArray< FNiagaraGPUSystemTick* , TMemStackAllocator<> > RelevantTicks;
		for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
		{
			FNiagaraComputeInstanceData* Data = Tick.GetInstanceData();
			FNiagaraComputeExecutionContext* SharedContext = Data->Context;
			// This assumes all emitters fallback to the same FNiagaraShaderScript*.
			FNiagaraShaderRef ComputeShader = SharedContext->GPUScript_RT->GetShader();
			if (ComputeShader.IsNull() || !ShouldTickForStage(Tick, TickStage))
			{
				continue;
			}

			Tick.bIsFinalTick = false; // @todo : this is true sometimes, needs investigation

			const bool bResetCounts = SharedContext->ScratchIndex == INDEX_NONE;
			if (bResetCounts)
			{
				RelevantContexts.Add(SharedContext);
			}
			// Here scratch index represent the index of the last tick
			SharedContext->ScratchIndex = RelevantTicks.Add(&Tick);

			// Allows us to count total required instances across all frames
			for (uint32 i = 0; i < Tick.Count; ++i)
			{
				FNiagaraComputeInstanceData& InstanceData = Tick.GetInstanceData()[i];
				FNiagaraComputeExecutionContext* ExecContext = InstanceData.Context;
				if (ExecContext == nullptr)
				{
					continue;
				}

				uint32 PrevNumInstances = 0;
				if (bResetCounts)
				{
					ExecContext->ScratchMaxInstances = InstanceData.SpawnInfo.MaxParticleCount;
					PrevNumInstances = Tick.bNeedsReset ? 0 : ExecContext->MainDataSet->GetCurrentData()->GetNumInstances();
				}
				else
				{
					PrevNumInstances = Tick.bNeedsReset ? 0 : ExecContext->ScratchNumInstances;
				}
				ExecContext->ScratchNumInstances = InstanceData.SpawnInfo.SpawnRateInstances + InstanceData.SpawnInfo.EventSpawnTotal + PrevNumInstances;

				const uint32 MaxInstanceCount = ExecContext->MainDataSet->GetMaxInstanceCount();
				ExecContext->ScratchNumInstances = FMath::Min(ExecContext->ScratchNumInstances, MaxInstanceCount);

				ExecContext->ScratchMaxInstances = FMath::Max(ExecContext->ScratchMaxInstances, ExecContext->ScratchNumInstances);
			}
		}

		// Set bIsFinalTick for the last tick of each context and reset the scratch index.
		const int32 ScrachIndexReset = UseOverlapCompute() ? 0 : INDEX_NONE;
		for (FNiagaraComputeExecutionContext* Context : RelevantContexts)
		{
			RelevantTicks[Context->ScratchIndex]->bIsFinalTick = true;
			Context->ScratchIndex = ScrachIndexReset;
		}

		if (UseOverlapCompute())
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

	// Clear any RT bindings that we may be using
	// Note: We can not encapsulate the whole Niagara pass as some DI's may not be compatable (i.e. use CopyTexture function), we need to fix this with future RDG conversion
	if (SimPasses.Num() > 0)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		RHICmdList.BeginComputePass(TEXT("NiagaraCompute"));
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		RHICmdList.BeginUAVOverlap();

		FEmitterInstanceList InstancesWithPersistentIDs;
		FNiagaraBufferArray OutputGraphicsBuffers;

		for (int32 SimPassIdx = 0; SimPassIdx < SimPasses.Num(); ++SimPassIdx)
		{
			FOverlappableTicks& SimPass = SimPasses[SimPassIdx];
			InstancesWithPersistentIDs.SetNum(0, false);

			// This initial pass gathers all the buffers that are read from and written to so we can do batch resource transitions.
			// It also ensures the GPU buffers are large enough to hold everything.
			ResizeBuffersAndGatherResources(SimPass, RHICmdList, OutputGraphicsBuffers, InstancesWithPersistentIDs);

			{
				SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUSimulation);
				SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSimulation);
				DispatchAllOnCompute(SimPass, RHICmdList, ViewUniformBuffer);
			}

			if (InstancesWithPersistentIDs.Num() == 0)
			{
				continue;
			}

			// If we're doing multiple ticks (e.g. when scrubbing the timeline in the editor), we must update the free ID buffers before running
			// the next tick, which will cause stalls (because the ID to index buffer is written by DispatchAllOnCompute and read by UpdateFreeIDBuffers).
			// However, when we're at the last tick, we can postpone the update until later in the frame and avoid the stall. This will be the case when
			// running normally, with one tick per frame.
			if (SimPassIdx < SimPasses.Num() - 1)
			{
				ResizeFreeIDsListSizesBuffer(InstancesWithPersistentIDs.Num());
				ClearFreeIDsListSizesBuffer(RHICmdList);
				UpdateFreeIDBuffers(RHICmdList, InstancesWithPersistentIDs);
			}
			else
			{
				DeferredIDBufferUpdates.Append(InstancesWithPersistentIDs);
				ResizeFreeIDsListSizesBuffer(DeferredIDBufferUpdates.Num());

				// Speculatively clear the list sizes buffer here. Under normal circumstances, this happens in the first stage which finds instances with persistent IDs
				// (usually PreInitViews) and it's finished by the time the deferred updates need to be processed. If a subsequent tick stage runs multiple time ticks,
				// the first step will find the buffer already cleared and will not clear again. The only time when this clear is superfluous is when a following stage
				// reallocates the buffer, but that's unlikely (and amortized) because we allocate in chunks.
				ClearFreeIDsListSizesBuffer(RHICmdList);
			}
		}

		OutputGraphicsBuffers.Add(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
		RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, OutputGraphicsBuffers.GetData(), OutputGraphicsBuffers.Num());

		RHICmdList.EndUAVOverlap();
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		RHICmdList.EndComputePass();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void NiagaraEmitterInstanceBatcher::PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate)
{
	if (!FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		return;
	}

	LLM_SCOPE(ELLMTag::Niagara);

	// Reset the list of GPUSort tasks and release any resources they hold on to.
	// It might be worth considering doing so at the end of the render to free the resources immediately.
	// (note that currently there are no callback appropriate to do it)
	SimulationsToSort.Reset();

	// Update draw indirect buffer to max possible size.
	if (bAllowGPUParticleUpdate)
	{
		int32 TotalDispatchCount = 0;
		for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
		{
			TotalDispatchCount += (int32)Tick.TotalDispatches;

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
									//UE_LOG(LogNiagara, Log, TEXT("GPU Readback Offset: %d %p = %d"), Context->EmitterInstanceReadback.GPUCountOffset, CurrentData, CurrentData->GetNumInstances());
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
			ExecuteAll(RHICmdList, nullptr, ETickStage::PreInitViews);
		}
	}
	else
	{
		GPUInstanceCounterManager.ResizeBuffers(RHICmdList, FeatureLevel,  0);
	}
}

void NiagaraEmitterInstanceBatcher::PostInitViews(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bAllowGPUParticleUpdate)
{
	if (!FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		return;
	}

	LLM_SCOPE(ELLMTag::Niagara);

	if (bAllowGPUParticleUpdate)
	{
		ExecuteAll(RHICmdList, ViewUniformBuffer, ETickStage::PostInitViews);
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
	if (!FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		return;
	}

	LLM_SCOPE(ELLMTag::Niagara);

	GlobalDistanceFieldParams = GlobalDistanceFieldParameterData ? *GlobalDistanceFieldParameterData : FGlobalDistanceFieldParameterData();
}

void NiagaraEmitterInstanceBatcher::OnDestroy()
{
	FNiagaraWorldManager::OnBatcherDestroyed(this);
	FFXSystemInterface::OnDestroy();
}

bool NiagaraEmitterInstanceBatcher::AddSortedGPUSimulation(FNiagaraGPUSortInfo& SortInfo)
{
	if (GPUSortManager && GPUSortManager->AddTask(SortInfo.AllocationInfo, SortInfo.ParticleCount, SortInfo.SortFlags))
	{
		// It's not worth currently to have a map between SortInfo.AllocationInfo.SortBatchId and the relevant indices in SimulationsToSort
		// because the number of batches is expect to be very small (1 or 2). If this change, it might be worth reconsidering.
		SimulationsToSort.Add(SortInfo);
		return true;
	}
	else
	{
		return false;
	}
}

void NiagaraEmitterInstanceBatcher::GenerateSortKeys(FRHICommandListImmediate& RHICmdList, int32 BatchId, int32 NumElementsInBatch, EGPUSortFlags Flags, FRHIUnorderedAccessView* KeysUAV, FRHIUnorderedAccessView* ValuesUAV)
{
	// Currently all Niagara KeyGen must execute after PreRender() - in between PreInitViews() and PostRenderOpaque(), when the GPU simulation are possibly ticked.
	check(EnumHasAnyFlags(Flags, EGPUSortFlags::KeyGenAfterPreRender));

	FRWBuffer* CulledCountsBuffer = GPUInstanceCounterManager.AcquireCulledCountsBuffer(RHICmdList, FeatureLevel);

	const FGPUSortManager::FKeyGenInfo KeyGenInfo((uint32)NumElementsInBatch, EnumHasAnyFlags(Flags, EGPUSortFlags::HighPrecisionKeys));

	FNiagaraSortKeyGenCS::FPermutationDomain SortPermutationVector;
	SortPermutationVector.Set<FNiagaraSortKeyGenCS::FSortUsingMaxPrecision>(GNiagaraGPUSortingUseMaxPrecision != 0);
	SortPermutationVector.Set<FNiagaraSortKeyGenCS::FEnableCulling>(false);

	FNiagaraSortKeyGenCS::FPermutationDomain SortAndCullPermutationVector;
	SortAndCullPermutationVector.Set<FNiagaraSortKeyGenCS::FSortUsingMaxPrecision>(GNiagaraGPUSortingUseMaxPrecision != 0);
	SortAndCullPermutationVector.Set<FNiagaraSortKeyGenCS::FEnableCulling>(true);

	TShaderMapRef<FNiagaraSortKeyGenCS> SortKeyGenCS(GetGlobalShaderMap(FeatureLevel), SortPermutationVector);
	TShaderMapRef<FNiagaraSortKeyGenCS> SortAndCullKeyGenCS(GetGlobalShaderMap(FeatureLevel), SortAndCullPermutationVector);
	
	FNiagaraSortKeyGenCS::FParameters Params;
	Params.SortKeyMask = KeyGenInfo.SortKeyParams.X;
	Params.SortKeyShift = KeyGenInfo.SortKeyParams.Y;
	Params.SortKeySignBit = KeyGenInfo.SortKeyParams.Z;
	Params.OutKeys = KeysUAV;
	Params.OutParticleIndices = ValuesUAV;
	Params.OutCulledParticleCounts = CulledCountsBuffer ? (FRHIUnorderedAccessView*)CulledCountsBuffer->UAV : GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, false);

	FRHIUnorderedAccessView* OutputUAVs[] = { KeysUAV, ValuesUAV };
	for (const FNiagaraGPUSortInfo& SortInfo : SimulationsToSort)
	{
		if (SortInfo.AllocationInfo.SortBatchId == BatchId)
		{
			Params.NiagaraParticleDataFloat = SortInfo.ParticleDataFloatSRV;
			Params.NiagaraParticleDataHalf = SortInfo.ParticleDataHalfSRV;
			Params.NiagaraParticleDataInt = SortInfo.ParticleDataIntSRV;
			Params.GPUParticleCountBuffer = SortInfo.GPUParticleCountSRV;
			Params.FloatDataStride = SortInfo.FloatDataStride;
			Params.HalfDataStride = SortInfo.HalfDataStride;
			Params.IntDataStride = SortInfo.IntDataStride;
			Params.ParticleCount = SortInfo.ParticleCount;
			Params.GPUParticleCountBuffer = SortInfo.GPUParticleCountSRV;
			Params.GPUParticleCountOffset = SortInfo.GPUParticleCountOffset;
			Params.CulledGPUParticleCountOffset = SortInfo.CulledGPUParticleCountOffset;
			Params.EmitterKey = (uint32)SortInfo.AllocationInfo.ElementIndex << KeyGenInfo.ElementKeyShift;
			Params.OutputOffset = SortInfo.AllocationInfo.BufferOffset;
			Params.CameraPosition = SortInfo.ViewOrigin;
			Params.CameraDirection = SortInfo.ViewDirection;
			Params.SortMode = (uint32)SortInfo.SortMode;
			Params.SortAttributeOffset = SortInfo.SortAttributeOffset;
			Params.CullPositionAttributeOffset = SortInfo.CullPositionAttributeOffset;
			Params.CullOrientationAttributeOffset = SortInfo.CullOrientationAttributeOffset;
			Params.CullScaleAttributeOffset = SortInfo.CullScaleAttributeOffset;
			Params.RendererVisibility = SortInfo.RendererVisibility;
			Params.RendererVisTagAttributeOffset = SortInfo.RendererVisTagAttributeOffset;
			Params.CullDistanceRangeSquared = SortInfo.DistanceCullRange * SortInfo.DistanceCullRange;
			Params.LocalBoundingSphere = FVector4(SortInfo.LocalBSphere.Center, SortInfo.LocalBSphere.W);

			Params.NumCullPlanes = 0;
			for (const FPlane& Plane : SortInfo.CullPlanes)
			{
				Params.CullPlanes[Params.NumCullPlanes++] = FVector4(Plane.X, Plane.Y, Plane.Z, Plane.W);
			}

			// Choose the shader to bind
			TShaderMapRef<FNiagaraSortKeyGenCS> KeyGenCS = SortInfo.bEnableCulling ? SortAndCullKeyGenCS : SortKeyGenCS;
			RHICmdList.SetComputeShader(KeyGenCS.GetComputeShader());

			SetShaderParameters(RHICmdList, KeyGenCS, KeyGenCS.GetComputeShader(), Params);
			DispatchComputeShader(RHICmdList, KeyGenCS, FMath::DivideAndRoundUp(SortInfo.ParticleCount, NIAGARA_KEY_GEN_THREAD_COUNT), 1, 1);			
			UnsetShaderUAVs(RHICmdList, KeyGenCS, KeyGenCS.GetComputeShader());
			
			// TR-KeyGen : No sync needed between tasks since they update different parts of the data (assuming it's ok if cache lines overlap).
			RHICmdList.TransitionResources(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, OutputUAVs, UE_ARRAY_COUNT(OutputUAVs));
		}
	}
}

void NiagaraEmitterInstanceBatcher::ProcessDebugInfo(FRHICommandList &RHICmdList, const FNiagaraComputeExecutionContext* Context) const
{
#if WITH_EDITORONLY_DATA
	// This method may be called from one of two places: in the tick or as part of a paused frame looking for the debug info that was submitted previously...
	// Note that PrevData is where we expect the data to be for rendering
	if (Context && Context->DebugInfo.IsValid())
	{
		// Fire off the readback if not already doing so
		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackHalf && !Context->GPUDebugDataReadbackCounts)
		{
			// Do nothing.., handled in Run
		}
		// We may not have floats or ints, but we should have at least one of the two
		else if (  (!Context->GPUDebugDataReadbackFloat || Context->GPUDebugDataReadbackFloat->IsReady())
				&& (!Context->GPUDebugDataReadbackInt || Context->GPUDebugDataReadbackInt->IsReady())
				&& (!Context->GPUDebugDataReadbackHalf || Context->GPUDebugDataReadbackHalf->IsReady())
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
				FFloat16* HalfDataBuffer = nullptr;
				if (Context->GPUDebugDataReadbackHalf)
				{
					HalfDataBuffer = static_cast<FFloat16*>(Context->GPUDebugDataReadbackHalf->Lock(Context->GPUDebugDataHalfSize));
				}

				Context->DebugInfo->Frame.CopyFromGPUReadback(FloatDataBuffer, IntDataBuffer, HalfDataBuffer, NewExistingDataCount, Context->GPUDebugDataFloatStride, Context->GPUDebugDataIntStride, Context->GPUDebugDataHalfStride);

				Context->DebugInfo->bWritten = true;

				if (Context->GPUDebugDataReadbackFloat)
				{
					Context->GPUDebugDataReadbackFloat->Unlock();
				}
				if (Context->GPUDebugDataReadbackInt)
				{
					Context->GPUDebugDataReadbackInt->Unlock();
				}
				if (Context->GPUDebugDataReadbackHalf)
				{
					Context->GPUDebugDataReadbackHalf->Unlock();
				}
				Context->GPUDebugDataReadbackCounts->Unlock();
			}
			{
				// The following code seems to take significant time on d3d12
				// Clear out the readback buffers...
				Context->GPUDebugDataReadbackFloat.Reset();
				Context->GPUDebugDataReadbackInt.Reset();
				Context->GPUDebugDataReadbackHalf.Reset();
				Context->GPUDebugDataReadbackCounts.Reset();

				Context->GPUDebugDataFloatSize = 0;
				Context->GPUDebugDataIntSize = 0;
				Context->GPUDebugDataHalfSize = 0;
				Context->GPUDebugDataFloatStride = 0;
				Context->GPUDebugDataIntStride = 0;
				Context->GPUDebugDataHalfStride = 0;
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
void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList& RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 SimulationStageIndex) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FNiagaraSystemInstanceID& SystemInstance = Tick.SystemInstanceID;
	const FNiagaraShaderMapPointerTable& PointerTable = Shader.GetPointerTable();

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			FNiagaraDataInterfaceSetArgs Context;
			Context.Shader = Shader;
			Context.DataInterface = Interface;
			Context.SystemInstance = SystemInstance;
			Context.Batcher = this;
			Context.ComputeInstanceData = Instance;
			Context.SimulationStageIndex = SimulationStageIndex;
			Context.IsOutputStage = Instance->IsOutputStage(Interface, SimulationStageIndex);
			DIParam.DIType.Get(PointerTable.DITypes)->SetParameters(DIParam.Parameters.Get(), RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*> &DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FNiagaraSystemInstanceID& SystemInstance = Tick.SystemInstanceID;
	const FNiagaraShaderMapPointerTable& PointerTable = Shader.GetPointerTable();

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
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
			DIParam.DIType.Get(PointerTable.DITypes)->UnsetParameters(DIParam.Parameters.Get(), RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}

static void SetConstantBuffer(FRHICommandList &RHICmdList, FRHIComputeShader* ComputeShader, const FShaderUniformBufferParameter& BufferParam, const FUniformBufferRHIRef& UniformBuffer)
{
	if (BufferParam.IsBound() && UniformBuffer.IsValid())
	{
		RHICmdList.SetShaderUniformBuffer(ComputeShader, BufferParam.GetBaseIndex(), UniformBuffer);
	}
}

void NiagaraEmitterInstanceBatcher::SetConstantBuffers(FRHICommandList &RHICmdList, const FNiagaraShaderRef& Shader, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData* Instance)
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;
	FRHIComputeShader* ComputeShader = Shader.GetComputeShader();

	SetConstantBuffer(RHICmdList, ComputeShader, Shader->GlobalConstantBufferParam[0], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_Global, nullptr, true));
	SetConstantBuffer(RHICmdList, ComputeShader, Shader->SystemConstantBufferParam[0], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_System, nullptr, true));
	SetConstantBuffer(RHICmdList, ComputeShader, Shader->OwnerConstantBufferParam[0], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_Owner, nullptr, true));
	SetConstantBuffer(RHICmdList, ComputeShader, Shader->EmitterConstantBufferParam[0], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_Emitter, Instance, true));
	SetConstantBuffer(RHICmdList, ComputeShader, Shader->ExternalConstantBufferParam[0], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_External, Instance, true));

	if (Context->HasInterpolationParameters)
	{
		SetConstantBuffer(RHICmdList, ComputeShader, Shader->GlobalConstantBufferParam[1], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_Global, nullptr, false));
		SetConstantBuffer(RHICmdList, ComputeShader, Shader->SystemConstantBufferParam[1], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_System, nullptr, false));
		SetConstantBuffer(RHICmdList, ComputeShader, Shader->OwnerConstantBufferParam[1], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_Owner, nullptr, false));
		SetConstantBuffer(RHICmdList, ComputeShader, Shader->EmitterConstantBufferParam[1], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_Emitter, Instance, false));
		SetConstantBuffer(RHICmdList, ComputeShader, Shader->ExternalConstantBufferParam[1], Tick.GetUniformBuffer(FNiagaraGPUSystemTick::UBT_External, Instance, false));
	}
}

/* Kick off a simulation/spawn run
 */
void NiagaraEmitterInstanceBatcher::Run(const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData* Instance, uint32 UpdateStartInstance, const uint32 TotalNumInstances, const FNiagaraShaderRef& Shader,
	FRHICommandList &RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const FNiagaraGpuSpawnInfo& SpawnInfo, bool bCopyBeforeStart, uint32 DefaultSimulationStageIndex, uint32 SimulationStageIndex, FNiagaraDataInterfaceProxy *IterationInterface, bool HasRunParticleStage)
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;


	if (TotalNumInstances == 0)
	{
		return;
	}

	/*UE_LOG(LogNiagara, Log, TEXT("Niagara Gpu Sim - % s - NumInstances: % u - StageNumber : % u"), Context->GetDebugSimName(),
		TotalNumInstances,
		SimulationStageIndex);
		*/

	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("Niagara Gpu Sim - %s - NumInstances: %u - StageNumber: %u - NumInstructions %u"),
		Context->GetDebugSimName(),
		TotalNumInstances,
		SimulationStageIndex,
		Shader->GetNumInstructions()
	);

	//UE_LOG(LogNiagara, Warning, TEXT("Run"));
	
	const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies = Instance->DataInterfaceProxies;
	check(Instance->SimStageData[SimulationStageIndex].Source && Instance->SimStageData[SimulationStageIndex].Destination);
	FNiagaraDataBuffer& DestinationData = *Instance->SimStageData[SimulationStageIndex].Destination;
	FNiagaraDataBuffer& CurrentData = *Instance->SimStageData[SimulationStageIndex].Source;

	//UE_LOG(LogScript, Warning, TEXT("Run [%d] TotalInstances %d  src:%p dest:%p"), SimulationStageIndex, TotalNumInstances, Instance->SimStageData[SimulationStageIndex].Source, Instance->SimStageData[SimulationStageIndex].Destination);

	int32 InstancesToSpawnThisFrame = DestinationData.GetNumSpawnedInstances();
	DestinationData.SetIDAcquireTag(FNiagaraComputeExecutionContext::TickCounter);

	// Only spawn particles on the first stage
	if (HasRunParticleStage)
	{
		InstancesToSpawnThisFrame = 0;
	}

	FRHIComputeShader* ComputeShader = Shader.GetComputeShader();
	RHICmdList.SetComputeShader(ComputeShader);

	// #todo(dmp): clean up this logic for shader stages on first frame
	SetShaderValue(RHICmdList, ComputeShader, Shader->SimStartParam, Tick.bNeedsReset ? 1U : 0U);

	// set the view uniform buffer param
	if (Shader->ViewUniformBufferParam.IsBound() && ViewUniformBuffer)
	{
		RHICmdList.SetShaderUniformBuffer(ComputeShader, Shader->ViewUniformBufferParam.GetBaseIndex(), ViewUniformBuffer);
	}

	SetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick, SimulationStageIndex);

	// set the shader and data set params 
	//
	const bool bRequiresPersistentIDs = Context->MainDataSet->RequiresPersistentIDs();
	SetSRVParameter(RHICmdList, Shader.GetComputeShader(), Shader->FreeIDBufferParam, bRequiresPersistentIDs ? Context->MainDataSet->GetGPUFreeIDs().SRV.GetReference() : FNiagaraRenderer::GetDummyIntBuffer());
	CurrentData.SetShaderParams(Shader.GetShader(), RHICmdList, true);
	DestinationData.SetShaderParams(Shader.GetShader(), RHICmdList, false);

	// set the instance count uav
	//
	if (Shader->InstanceCountsParam.IsBound())
	{
		RHICmdList.TransitionResource(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
		Shader->InstanceCountsParam.SetBuffer(RHICmdList, ComputeShader, GPUInstanceCounterManager.GetInstanceCountBuffer());

		if (IterationInterface)
		{
			SetShaderValue(RHICmdList, ComputeShader, Shader->ReadInstanceCountOffsetParam, -1);
			SetShaderValue(RHICmdList, ComputeShader, Shader->WriteInstanceCountOffsetParam, -1);
		}
		else
		{
			const uint32 ReadOffset = (Tick.bNeedsReset && SimulationStageIndex == 0) ? INDEX_NONE : Instance->SimStageData[SimulationStageIndex].SourceCountOffset;
			const uint32 WriteOffset = Instance->SimStageData[SimulationStageIndex].DestinationCountOffset;
			//UE_LOG(LogNiagara, Log, TEXT("Instance count setup R: %d W: %d reset? %s %d"), ReadOffset, WriteOffset, Tick.bNeedsReset ? TEXT("T") : TEXT("F"), CurrentData.GetGPUInstanceCountBufferOffset());
			SetShaderValue(RHICmdList, ComputeShader, Shader->ReadInstanceCountOffsetParam, ReadOffset);
			SetShaderValue(RHICmdList, ComputeShader, Shader->WriteInstanceCountOffsetParam, WriteOffset);
		}
	}

	// set the execution parameters
	//
	SetShaderValue(RHICmdList, ComputeShader, Shader->EmitterTickCounterParam, FNiagaraComputeExecutionContext::TickCounter);

	// set spawn info
	//
	static_assert((sizeof(SpawnInfo.SpawnInfoStartOffsets) % SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) == 0, "sizeof SpawnInfoStartOffsets should be a multiple of SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT");
	static_assert((sizeof(SpawnInfo.SpawnInfoParams) % SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) == 0, "sizeof SpawnInfoParams should be a multiple of SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT");
	SetShaderValueArray(RHICmdList, ComputeShader, Shader->EmitterSpawnInfoOffsetsParam, SpawnInfo.SpawnInfoStartOffsets, NIAGARA_MAX_GPU_SPAWN_INFOS);
	// This parameter is an array of structs with 2 floats and 2 ints on CPU, but a float4 array on GPU. The shader uses asint() to cast the integer values. To set the parameter, 
	// we pass the structure array as a float* to SetShaderValueArray() and specify the number of floats (not float vectors).
	SetShaderValueArray(RHICmdList, ComputeShader, Shader->EmitterSpawnInfoParamsParam, &SpawnInfo.SpawnInfoParams[0].IntervalDt, 4*NIAGARA_MAX_GPU_SPAWN_INFOS);

	SetShaderValue(RHICmdList, ComputeShader, Shader->UpdateStartInstanceParam, UpdateStartInstance);					// 0, except for event handler runs
	SetShaderValue(RHICmdList, ComputeShader, Shader->NumSpawnedInstancesParam, InstancesToSpawnThisFrame);				// number of instances in the spawn run
	SetShaderValue(RHICmdList, ComputeShader, Shader->DefaultSimulationStageIndexParam, DefaultSimulationStageIndex);					// 0, except if several stages are defined
	SetShaderValue(RHICmdList, ComputeShader, Shader->SimulationStageIndexParam, SimulationStageIndex);					// 0, except if several stages are defined
	const int32 DefaultIterationCount = -1;
	SetShaderValue(RHICmdList, ComputeShader, Shader->IterationInterfaceCount, DefaultIterationCount);					// 0, except if several stages are defined

	const uint32 ShaderThreadGroupSize = FNiagaraShader::GetGroupSize(ShaderPlatform);
	if (IterationInterface)
	{
		if (TotalNumInstances > ShaderThreadGroupSize)
		{
			SetShaderValue(RHICmdList, ComputeShader, Shader->IterationInterfaceCount, TotalNumInstances);					// 0, except if several stages are defined
		}
	}

	uint32 NumThreadGroups = 1;
	if (TotalNumInstances > ShaderThreadGroupSize)
	{
		NumThreadGroups = FMath::Min(NIAGARA_MAX_COMPUTE_THREADGROUPS, FMath::DivideAndRoundUp(TotalNumInstances, ShaderThreadGroupSize));
	}

	SetConstantBuffers(RHICmdList, Shader, Tick, Instance);

	//UE_LOG(LogNiagara, Log, TEXT("Num Instance : %d | Num Group : %d | Spawned Istance : %d | Start Instance : %d | Num Indices : %d | Stage Index : %d"), 
		//TotalNumInstances, NumThreadGroups, InstancesToSpawnThisFrame, UpdateStartInstance, Context->NumIndicesPerInstance, SimulationStageIndex);

	// Dispatch, if anything needs to be done
	if (TotalNumInstances)
	{
		DispatchComputeShader(RHICmdList, Shader.GetShader(), NumThreadGroups, 1, 1);
	}

	// reset iteration count
	if (IterationInterface)
	{
		SetShaderValue(RHICmdList, ComputeShader, Shader->IterationInterfaceCount, DefaultIterationCount);					// 0, except if several stages are defined
	}

#if WITH_EDITORONLY_DATA
	// Check to see if we need to queue up a debug dump..
	if (Context->DebugInfo.IsValid())
	{
		//UE_LOG(LogNiagara, Warning, TEXT("Queued up!"));

		if (!Context->GPUDebugDataReadbackFloat && !Context->GPUDebugDataReadbackInt && !Context->GPUDebugDataReadbackHalf && !Context->GPUDebugDataReadbackCounts && DestinationData.GetGPUInstanceCountBufferOffset() != INDEX_NONE && SimulationStageIndex == Context->MaxUpdateIterations - 1)
		{
			Context->GPUDebugDataFloatSize = 0;
			Context->GPUDebugDataIntSize = 0;
			Context->GPUDebugDataFloatStride = 0;
			Context->GPUDebugDataIntStride = 0;

			if (DestinationData.GetGPUBufferFloat().NumBytes > 0)
			{
				static const FName ReadbackFloatName(TEXT("Niagara GPU Debug Info Float Emitter Readback"));
				Context->GPUDebugDataReadbackFloat.Reset(new FRHIGPUBufferReadback(ReadbackFloatName));
				Context->GPUDebugDataReadbackFloat->EnqueueCopy(RHICmdList, DestinationData.GetGPUBufferFloat().Buffer);
				Context->GPUDebugDataFloatSize = DestinationData.GetGPUBufferFloat().NumBytes;
				Context->GPUDebugDataFloatStride = DestinationData.GetFloatStride();
			}

			if (DestinationData.GetGPUBufferInt().NumBytes > 0)
			{
				static const FName ReadbackIntName(TEXT("Niagara GPU Debug Info Int Emitter Readback"));
				Context->GPUDebugDataReadbackInt.Reset(new FRHIGPUBufferReadback(ReadbackIntName));
				Context->GPUDebugDataReadbackInt->EnqueueCopy(RHICmdList, DestinationData.GetGPUBufferInt().Buffer);
				Context->GPUDebugDataIntSize = DestinationData.GetGPUBufferInt().NumBytes;
				Context->GPUDebugDataIntStride = DestinationData.GetInt32Stride();
			}

			if (DestinationData.GetGPUBufferHalf().NumBytes > 0)
			{
				static const FName ReadbackHalfName(TEXT("Niagara GPU Debug Info Half Emitter Readback"));
				Context->GPUDebugDataReadbackHalf.Reset(new FRHIGPUBufferReadback(ReadbackHalfName));
				Context->GPUDebugDataReadbackHalf->EnqueueCopy(RHICmdList, DestinationData.GetGPUBufferHalf().Buffer);
				Context->GPUDebugDataHalfSize = DestinationData.GetGPUBufferHalf().NumBytes;
				Context->GPUDebugDataHalfStride = DestinationData.GetHalfStride();
			}

			static const FName ReadbackCountsName(TEXT("Niagara GPU Emitter Readback"));
			Context->GPUDebugDataReadbackCounts.Reset(new FRHIGPUBufferReadback(ReadbackCountsName));
			Context->GPUDebugDataReadbackCounts->EnqueueCopy(RHICmdList, GPUInstanceCounterManager.GetInstanceCountBuffer().Buffer);
			Context->GPUDebugDataCountOffset = DestinationData.GetGPUInstanceCountBufferOffset();
		}
	}
#endif // WITH_EDITORONLY_DATA

	// Unset UAV parameters and transition resources (TODO: resource transition should be moved to the renderer)
	// 
	UnsetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick);
	CurrentData.UnsetShaderParams(Shader.GetShader(), RHICmdList);
	DestinationData.UnsetShaderParams(Shader.GetShader(), RHICmdList);
	Shader->InstanceCountsParam.UnsetUAV(RHICmdList, ComputeShader);

	ResetEmptyUAVPools(RHICmdList);
}

FGPUSortManager* NiagaraEmitterInstanceBatcher::GetGPUSortManager() const
{
	return GPUSortManager;
}

NiagaraEmitterInstanceBatcher::DummyUAV::~DummyUAV()
{
	UAV.SafeRelease();
	Buffer.SafeRelease();
	Texture.SafeRelease();
}

void NiagaraEmitterInstanceBatcher::DummyUAV::Init(FRHICommandList& RHICmdList, EPixelFormat Format, bool IsTexture, const TCHAR* DebugName)
{
	checkSlow(IsInRenderingThread());

	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.DebugName = DebugName;

	if (IsTexture)
	{
		Texture = RHICreateTexture2D(1, 1, Format, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
		UAV = RHICreateUnorderedAccessView(Texture, 0);
	}
	else
	{
		uint32 BytesPerElement = GPixelFormats[Format].BlockBytes;
		Buffer = RHICreateVertexBuffer(BytesPerElement, BUF_UnorderedAccess | BUF_ShaderResource, CreateInfo);
		UAV = RHICreateUnorderedAccessView(Buffer, Format);
	}

	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, UAV);
}

FRHIUnorderedAccessView* NiagaraEmitterInstanceBatcher::GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, bool IsTexture) const
{
	TMap<EPixelFormat, DummyUAVPool>& UAVMap = IsTexture ? DummyTexturePool : DummyBufferPool;
	DummyUAVPool& Pool = UAVMap.FindOrAdd(Format);
	checkSlow(Pool.NextFreeIndex <= Pool.UAVs.Num());
	if (Pool.NextFreeIndex == Pool.UAVs.Num())
	{
		DummyUAV& NewUAV = Pool.UAVs.AddDefaulted_GetRef();
		NewUAV.Init(RHICmdList, Format, IsTexture, TEXT("NiagaraEmitterInstanceBatcher::DummyUAV"));
	}

	FRHIUnorderedAccessView* UAV = Pool.UAVs[Pool.NextFreeIndex].UAV;
	++Pool.NextFreeIndex;
	return UAV;
}

void NiagaraEmitterInstanceBatcher::ResetEmptyUAVPool(TMap<EPixelFormat, DummyUAVPool>& UAVMap, TArray<FRHIUnorderedAccessView*>& Transitions)
{
	for (TPair<EPixelFormat, DummyUAVPool>& Entry : UAVMap)
	{
		for (int UsedIdx = 0; UsedIdx < Entry.Value.NextFreeIndex; ++UsedIdx)
		{
			Transitions.Add(Entry.Value.UAVs[UsedIdx].UAV);
		}
		Entry.Value.NextFreeIndex = 0;
	}
}

void NiagaraEmitterInstanceBatcher::ResetEmptyUAVPools(FRHICommandList& RHICmdList)
{
	TArray<FRHIUnorderedAccessView*> Transitions;
	Transitions.Reserve(32);
	ResetEmptyUAVPool(DummyBufferPool, Transitions);
	ResetEmptyUAVPool(DummyTexturePool, Transitions);
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, Transitions.GetData(), Transitions.Num());
}
