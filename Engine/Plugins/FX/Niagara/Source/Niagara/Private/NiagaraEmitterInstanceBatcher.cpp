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
#include "NiagaraDataInterfaceRW.h"
#if WITH_EDITOR
#include "NiagaraGpuComputeDebug.h"
#endif
#include "NiagaraGpuReadbackManager.h"
#include "ShaderParameterUtils.h"
#include "SceneUtils.h"
#include "ClearQuad.h"
#include "Async/Async.h"
#include "GPUSort.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "Runtime/Renderer/Private/PostProcess/SceneRenderTargets.h"

DECLARE_CYCLE_STAT(TEXT("Niagara Dispatch Setup"), STAT_NiagaraGPUDispatchSetup_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Emitter Dispatch [RT]"), STAT_NiagaraGPUSimTick_RT, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("GPU Data Readback [RT]"), STAT_NiagaraGPUReadback_RT, STATGROUP_Niagara);
DECLARE_FLOAT_COUNTER_STAT(TEXT("Niagara GPU Sim"), STAT_GPU_NiagaraSim, STATGROUP_GPU);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Particles"), STAT_NiagaraGPUParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Particles"), STAT_NiagaraGPUSortedParticles, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Sorted Buffers"), STAT_NiagaraGPUSortedBuffers, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("Readback latency (frames)"), STAT_NiagaraReadbackLatency, STATGROUP_Niagara);
DECLARE_DWORD_COUNTER_STAT(TEXT("# GPU Dispatches"), STAT_NiagaraGPUDispatches, STATGROUP_Niagara);

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

int32 GNiagaraGpuMaxQueuedRenderFrames = 10;
static FAutoConsoleVariableRef CVarNiagaraGpuMaxQueuedRenderFrames(
	TEXT("fx.Niagara.Batcher.MaxQueuedFramesWithoutRender"),
	GNiagaraGpuMaxQueuedRenderFrames,
	TEXT("Number of frames we allow to be queued before we force ticks to be released or executed.\n"),
	ECVF_Default
);

int32 GNiagaraGpuSubmitCommandHint = WITH_EDITOR ? 10 : 0;
static FAutoConsoleVariableRef CVarNiagaraGpuSubmitCommandHint(
	TEXT("fx.NiagaraGpuSubmitCommandHint"),
	GNiagaraGpuSubmitCommandHint,
	TEXT("If greater than zero, we use this value to submit commands after the number of dispatches have been issued."),
	ECVF_Default
);

int32 GNiagaraGpuLowLatencyTranslucencyEnabled = 0;
static FAutoConsoleVariableRef CVarNiagaraGpuLowLatencyTranslucencyEnabled(
	TEXT("fx.NiagaraGpuLowLatencyTranslucencyEnabled"),
	GNiagaraGpuLowLatencyTranslucencyEnabled,
	TEXT("When enabled translucent materials can use the current frames simulation data no matter which tick pass Niagara uses.\n")
	TEXT("This can result in an additional data buffer being required but will reduce any latency when using view uniform buffer / depth buffer / distance fields / etc"),
	ECVF_Default
);

const FName NiagaraEmitterInstanceBatcher::Name(TEXT("NiagaraEmitterInstanceBatcher"));

namespace NiagaraEmitterInstanceBatcherLocal
{
	int32 GTickFlushMaxQueuedFrames = 10;
	static FAutoConsoleVariableRef CVarNiagaraTickFlushMaxQueuedFrames(
		TEXT("fx.Niagara.Batcher.TickFlush.MaxQueuedFrames"),
		GTickFlushMaxQueuedFrames,
		TEXT("The number of unprocessed frames with queued ticks before we process them.\n")
		TEXT("The larger the number the more data we process in a single frame, this is generally only a concern when the application does not have focus."),
		ECVF_Default
	);

	int32 GTickFlushMode = 1;
	static FAutoConsoleVariableRef CVarNiagaraTickFlushMode(
		TEXT("fx.Niagara.Batcher.TickFlush.Mode"),
		GTickFlushMode,
		TEXT("What to do when we go over our max queued frames.\n")
		TEXT("0 = Keep ticks queued, can result in a long pause when gaining focus again.\n")
		TEXT("1 = (Default) Process all queued ticks with dummy view / buffer data, may result in incorrect simulation due to missing depth collisions, etc.\n")
		TEXT("2 = Kill all pending ticks, may result in incorrect simulation due to missing frames of data, i.e. a particle reset.\n"),
		ECVF_Default
	);

	static ETickStage CalculateTickStage(const FNiagaraGPUSystemTick& Tick)
	{
		if (!GNiagaraAllowTickBeforeRender || Tick.bRequiresDistanceFieldData || Tick.bRequiresDepthBuffer)
		{
			return ETickStage::PostOpaqueRender;
		}

		if (Tick.bRequiresEarlyViewData)
		{
			return ETickStage::PostInitViews;
		}

		if (Tick.bRequiresViewUniformBuffer)
		{
			return ETickStage::PostOpaqueRender;
		}
		return ETickStage::PreInitViews;
	}

	static bool ShouldRunStage(const FNiagaraGPUSystemTick* Tick, FNiagaraComputeExecutionContext* Context, FNiagaraDataInterfaceProxyRW* IterationInterface, uint32 StageIndex)
	{
		if (!IterationInterface || Context->SpawnStages.Num() == 0)
		{
			return true;
		}

		const bool bIsSpawnStage = Context->SpawnStages.Contains(StageIndex);
		return Tick->bNeedsReset == bIsSpawnStage;
	}
}

FFXSystemInterface* NiagaraEmitterInstanceBatcher::GetInterface(const FName& InName)
{
	return InName == Name ? this : nullptr;
}

NiagaraEmitterInstanceBatcher::NiagaraEmitterInstanceBatcher(ERHIFeatureLevel::Type InFeatureLevel, EShaderPlatform InShaderPlatform, FGPUSortManager* InGPUSortManager)
	: FeatureLevel(InFeatureLevel)
	, ShaderPlatform(InShaderPlatform)
	, GPUSortManager(InGPUSortManager)
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

	GlobalCBufferLayout = new FNiagaraRHIUniformBufferLayout(TEXT("Niagara GPU Global CBuffer"));
	GlobalCBufferLayout->UBLayout.ConstantBufferSize = sizeof(FNiagaraGlobalParameters);
	GlobalCBufferLayout->UBLayout.ComputeHash();

	SystemCBufferLayout = new FNiagaraRHIUniformBufferLayout(TEXT("Niagara GPU System CBuffer"));
	SystemCBufferLayout->UBLayout.ConstantBufferSize = sizeof(FNiagaraSystemParameters);
	SystemCBufferLayout->UBLayout.ComputeHash();

	OwnerCBufferLayout = new FNiagaraRHIUniformBufferLayout(TEXT("Niagara GPU Owner CBuffer"));
	OwnerCBufferLayout->UBLayout.ConstantBufferSize = sizeof(FNiagaraOwnerParameters);
	OwnerCBufferLayout->UBLayout.ComputeHash();

	EmitterCBufferLayout = new FNiagaraRHIUniformBufferLayout(TEXT("Niagara GPU Emitter CBuffer"));
	EmitterCBufferLayout->UBLayout.ConstantBufferSize = sizeof(FNiagaraEmitterParameters);
	EmitterCBufferLayout->UBLayout.ComputeHash();

#if NIAGARA_COMPUTEDEBUG_ENABLED
	GpuComputeDebugPtr.Reset(new FNiagaraGpuComputeDebug(FeatureLevel));
#endif
	GpuReadbackManagerPtr.Reset(new FNiagaraGpuReadbackManager());
}

NiagaraEmitterInstanceBatcher::~NiagaraEmitterInstanceBatcher()
{
	FinishDispatches();

	GlobalCBufferLayout = nullptr;
	SystemCBufferLayout = nullptr;
	OwnerCBufferLayout = nullptr;
	EmitterCBufferLayout = nullptr;
}

void NiagaraEmitterInstanceBatcher::InstanceDeallocated_RenderThread(const FNiagaraSystemInstanceID InstanceID)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	if (FNiagaraGpuComputeDebug* GpuComputeDebug = GpuComputeDebugPtr.Get())
	{
		GpuComputeDebug->OnSystemDeallocated(InstanceID);
	}

	GpuDebugReadbackInfos.RemoveAll(
		[&](const FDebugReadbackInfo& Info)
		{
			// In the unlikely event we have on in the queue make sure it's marked as complete with no data in it
			Info.DebugInfo->Frame.CopyFromGPUReadback(nullptr, nullptr, nullptr, 0, 0, 0, 0, 0);
			Info.DebugInfo->bWritten = true;
			return Info.InstanceID == InstanceID;
		}
	);
#endif

	int iTick = 0;
	while ( iTick < Ticks_RT.Num() )
	{
		FNiagaraGPUSystemTick& Tick = Ticks_RT[iTick];
		if (Tick.SystemInstanceID == InstanceID)
		{
			ensure(NumTicksThatRequireDistanceFieldData >= (Tick.bRequiresDistanceFieldData ? 1u : 0u));
			ensure(NumTicksThatRequireDepthBuffer >= (Tick.bRequiresDepthBuffer ? 1u : 0u));
			ensure(NumTicksThatRequireEarlyViewData >= (Tick.bRequiresEarlyViewData ? 1u : 0u));

			NumTicksThatRequireDistanceFieldData -= Tick.bRequiresDistanceFieldData ? 1 : 0;
			NumTicksThatRequireDepthBuffer -= Tick.bRequiresDepthBuffer ? 1 : 0;
			NumTicksThatRequireEarlyViewData -= Tick.bRequiresEarlyViewData ? 1 : 0;

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

void NiagaraEmitterInstanceBatcher::BuildConstantBuffers(FNiagaraGPUSystemTick& Tick) const
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
	FMemory::Memzero(BoundParameterCounts);

	for (uint32 CountIt = 0; CountIt < Tick.Count; ++CountIt)
	{
		FNiagaraComputeInstanceData& EmitterData = EmittersData[CountIt];

		for (int32 InterpIt = 0; InterpIt < (HasInterpolationParameters ? 2 : 1); ++InterpIt)
		{
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Global][InterpIt] += EmitterData.Context->GPUScript_RT->IsGlobalConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_System][InterpIt] += EmitterData.Context->GPUScript_RT->IsSystemConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Owner][InterpIt] += EmitterData.Context->GPUScript_RT->IsOwnerConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Emitter][InterpIt] += EmitterData.Context->GPUScript_RT->IsEmitterConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_External][InterpIt] += EmitterData.Context->GPUScript_RT->IsExternalConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
		}
	}

	const int32 InterpScale = HasInterpolationParameters ? 2 : 1;
	const int32 BufferCount = InterpScale * (FNiagaraGPUSystemTick::UBT_NumSystemTypes + FNiagaraGPUSystemTick::UBT_NumInstanceTypes * Tick.Count);

	Tick.UniformBuffers.Empty(BufferCount);

	const FRHIUniformBufferLayout* SystemLayouts[FNiagaraGPUSystemTick::UBT_NumSystemTypes] =
	{
		&GlobalCBufferLayout->UBLayout,
		&SystemCBufferLayout->UBLayout,
		&OwnerCBufferLayout->UBLayout
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
					FRHIUniformBufferLayout& Layout = InstanceTypeIt == FNiagaraGPUSystemTick::UBT_Emitter ? EmitterCBufferLayout->UBLayout : EmitterData.Context->ExternalCBufferLayout->UBLayout;
					if (Layout.Resources.Num() > 0 || Layout.ConstantBufferSize > 0)
					{
						BufferRef = RHICreateUniformBuffer(
							Tick.GetUniformBufferSource((FNiagaraGPUSystemTick::EUniformBufferType) InstanceTypeIt, &EmitterData, !InterpIt),
							Layout,
							((BoundParameterCounts[InstanceTypeIt][InterpIt] > 1) || HasMultipleStages)
							? EUniformBufferUsage::UniformBuffer_SingleFrame
							: EUniformBufferUsage::UniformBuffer_SingleDraw);
					}
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

	NumTicksThatRequireDistanceFieldData += Tick.bRequiresDistanceFieldData ? 1 : 0;
	NumTicksThatRequireDepthBuffer += Tick.bRequiresDepthBuffer ? 1 : 0;
	NumTicksThatRequireEarlyViewData += Tick.bRequiresEarlyViewData ? 1 : 0;
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

void NiagaraEmitterInstanceBatcher::Tick(float DeltaTime)
{
	check(IsInGameThread());
	ENQUEUE_RENDER_COMMAND(NiagaraPumpBatcher)(
		[RT_NiagaraBatcher=this](FRHICommandListImmediate& RHICmdList)
		{
			RT_NiagaraBatcher->ProcessPendingTicksFlush(RHICmdList);
		}
	);
}

void NiagaraEmitterInstanceBatcher::ProcessPendingTicksFlush(FRHICommandListImmediate& RHICmdList)
{
	// No ticks are pending
	if ( Ticks_RT.Num() == 0 )
	{
		FramesBeforeTickFlush = 0;
		return;
	}

	// We have pending ticks increment our counter, once we cross the threshold we will perform the appropriate operation
	++FramesBeforeTickFlush;
	if (FramesBeforeTickFlush < uint32(NiagaraEmitterInstanceBatcherLocal::GTickFlushMaxQueuedFrames) )
	{
		return;
	}
	FramesBeforeTickFlush = 0;

	switch (NiagaraEmitterInstanceBatcherLocal::GTickFlushMode)
	{
		// Do nothing
		default:
		case 0:
		{
			//UE_LOG(LogNiagara, Log, TEXT("NiagaraEmitterInstanceBatcher: Queued ticks (%d) are building up, this may cause a stall when released."), Ticks_RT.Num());
			break;
		}

		// Process all the pending ticks that have built up
		case 1:
		{
			//UE_LOG(LogNiagara, Log, TEXT("NiagaraEmitterInstanceBatcher: Queued ticks are being Processed due to not rendering.  This may result in undesirable simulation artifacts."));

			// Make a temporary ViewInfo
			//-TODO: We could gather some more information here perhaps?
			FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(nullptr, nullptr, FEngineShowFlags(ESFIM_Game))
				.SetWorldTimes(0, 0, 0)
				.SetGammaCorrection(1.0f));

			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.ViewFamily = &ViewFamily;
			ViewInitOptions.SetViewRectangle(FIntRect(0, 0, 128, 128));
			ViewInitOptions.ViewOrigin = FVector::ZeroVector;
			ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
			ViewInitOptions.ProjectionMatrix = FMatrix::Identity;

			FViewInfo View(ViewInitOptions);
			View.CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(FRHICommandListExecutor::GetImmediateCommandList());
			FBox UnusedVolumeBounds[TVC_MAX];
			View.SetupUniformBufferParameters(SceneContext, UnusedVolumeBounds, TVC_MAX, *View.CachedViewUniformShaderParameters);

			View.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*View.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

			// Execute all ticks that we can support without invalid simulations
			PreInitViews(RHICmdList, true);
			PostInitViews(RHICmdList, View.ViewUniformBuffer, true);
			PostRenderOpaque(RHICmdList, View.ViewUniformBuffer, &FSceneTextureUniformParameters::StaticStructMetadata, CreateSceneTextureUniformBuffer(RHICmdList, FeatureLevel), true);
			break;
		}

		// Kill all the pending ticks that have built up
		case 2:
		{
			//UE_LOG(LogNiagara, Log, TEXT("NiagaraEmitterInstanceBatcher: Queued ticks are being Destroyed due to not rendering.  This may result in undesirable simulation artifacts."));

			FinishDispatches();
			break;
		}
	}
}

void NiagaraEmitterInstanceBatcher::FinishDispatches()
{
	ReleaseTicks();

	NumTicksThatRequireDistanceFieldData = 0;
	NumTicksThatRequireDepthBuffer = 0;
	NumTicksThatRequireEarlyViewData = 0;

	for (int32 iTickStage=0; iTickStage < (int)ETickStage::Max; ++iTickStage)
	{
		ContextsPerStage[iTickStage].Reset();
		TicksPerStage[iTickStage].Reset();
	}
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

bool NiagaraEmitterInstanceBatcher::ResetDataInterfaces(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData* Instance, FRHICommandList &RHICmdList, const FNiagaraShaderScript* ShaderScript) const
{
	bool ValidSpawnStage = true;
	FNiagaraComputeExecutionContext* Context = Instance->Context;

	// Reset all rw data interface data
	if (Tick.bNeedsReset)
	{
		// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
		const FNiagaraShaderRef ComputeShader = ShaderScript->GetShader(0);

		uint32 InterfaceIndex = 0;
		for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
		{
			const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
			if (DIParam.Parameters.IsValid())
			{
				const FNiagaraDataInterfaceArgs TmpContext(Interface, Tick.SystemInstanceID, this);
				Interface->ResetData(RHICmdList, TmpContext);
			}			
			InterfaceIndex++;
		}
	}
	return ValidSpawnStage;
}

FNiagaraDataInterfaceProxyRW* NiagaraEmitterInstanceBatcher::FindIterationInterface(FNiagaraComputeInstanceData* Instance, const uint32 SimulationStageIndex) const
{
	// Determine if the iteration is outputting to a custom data size
	return Instance->FindIterationInterface(SimulationStageIndex);
}

void NiagaraEmitterInstanceBatcher::PreStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData* Instance, FRHICommandList& RHICmdList, const uint32 SimulationStageIndex) const
{
	if (!Instance->Context || !Instance->Context->GPUScript_RT->IsShaderMapComplete_RenderThread())
	{
		return;
	}

	// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
	const FNiagaraShaderRef& ComputeShader = Instance->Context->GPUScript_RT->GetShader(0);

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			const FNiagaraDataInterfaceStageArgs TmpContext(Interface, Tick.SystemInstanceID, this, Instance, SimulationStageIndex, Instance->IsOutputStage(Interface, SimulationStageIndex), Instance->IsIterationStage(Interface, SimulationStageIndex));
			Interface->PreStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::PostStageInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData* Instance, FRHICommandList &RHICmdList, const uint32 SimulationStageIndex) const
{
	if (!Instance->Context || !Instance->Context->GPUScript_RT->IsShaderMapComplete_RenderThread())
	{
		return;
	}

	// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
	const FNiagaraShaderRef& ComputeShader = Instance->Context->GPUScript_RT->GetShader(0);

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			const FNiagaraDataInterfaceStageArgs TmpContext(Interface, Tick.SystemInstanceID, this, Instance, SimulationStageIndex, Instance->IsOutputStage(Interface, SimulationStageIndex), Instance->IsIterationStage(Interface, SimulationStageIndex));
			Interface->PostStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::PostSimulateInterface(const FNiagaraGPUSystemTick& Tick, FNiagaraComputeInstanceData* Instance, FRHICommandList& RHICmdList, const FNiagaraShaderScript* ShaderScript) const
{
	// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
	const FNiagaraShaderRef ComputeShader = ShaderScript->GetShader(0);

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : Instance->DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			const FNiagaraDataInterfaceArgs TmpContext(Interface, Tick.SystemInstanceID, this);
			Interface->PostSimulate(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::DispatchStage(FDispatchInstance& DispatchInstance, uint32 StageIndex, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer)
{
	FNiagaraGPUSystemTick& Tick = *DispatchInstance.Tick;
	FNiagaraComputeInstanceData* Instance = DispatchInstance.InstanceData;
	FNiagaraComputeExecutionContext* Context = Instance->Context;

	if (!Context || !Context->GPUScript_RT->IsShaderMapComplete_RenderThread())
	{
		return;
	}

	if (StageIndex == 0)
	{
		FNiagaraComputeExecutionContext::TickCounter++;
		if (!ResetDataInterfaces(Tick, Instance, RHICmdList, Context->GPUScript_RT))
		{
			return;
		}
	}

	const int32 PermutationId = Tick.NumInstancesWithSimStages > 0 ? Context->GPUScript_RT->ShaderStageIndexToPermutationId_RenderThread(StageIndex) : 0;
	const FNiagaraShaderRef ComputeShader = Context->GPUScript_RT->GetShader(PermutationId);

	const uint32 DefaultSimulationStageIndex = Instance->Context->DefaultSimulationStageIndex;
	FNiagaraDataInterfaceProxyRW* IterationInterface = Instance->SimStageData[StageIndex].AlternateIterationSource;
	if (!IterationInterface)
	{
		Run(Tick, Instance, 0, Instance->SimStageData[StageIndex].DestinationNumInstances, ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo, false, DefaultSimulationStageIndex, StageIndex, nullptr, StageIndex > 0);
	}
	else
	{
		const FIntVector ElementCount = IterationInterface->GetElementCount(Tick.SystemInstanceID);
		const uint64 TotalNumInstances = ElementCount.X * ElementCount.Y * ElementCount.Z;

		// Verify the number of elements isn't higher that what we can handle
		checkf(TotalNumInstances < uint64(TNumericLimits<int32>::Max()), TEXT("ElementCount(%d, %d, %d) for IterationInterface(%s) overflows an int32 this is not allowed"), ElementCount.X, ElementCount.Y, ElementCount.Z, *IterationInterface->SourceDIName.ToString());

		Run(Tick, Instance, 0, uint32(TotalNumInstances), ComputeShader, RHICmdList, ViewUniformBuffer, Instance->SpawnInfo, false, DefaultSimulationStageIndex, StageIndex, IterationInterface);
	}
}

void NiagaraEmitterInstanceBatcher::AddDestinationBufferTransitions(FDispatchGroup* Group, FNiagaraDataBuffer* DestinationData)
{
	auto AddBufferTransitions = [](FDispatchGroup* Group, FRHIUnorderedAccessView* Buffer)
	{
		if (Buffer)
		{
			Group->TransitionsBefore.Add(FRHITransitionInfo(Buffer, ERHIAccess::SRVMask, ERHIAccess::UAVCompute));
			Group->TransitionsAfter.Add(FRHITransitionInfo(Buffer, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		}
	};

	AddBufferTransitions(Group, DestinationData->GetGPUBufferFloat().UAV);
	AddBufferTransitions(Group, DestinationData->GetGPUBufferHalf().UAV);
	AddBufferTransitions(Group, DestinationData->GetGPUBufferInt().UAV);

}

void NiagaraEmitterInstanceBatcher::BuildDispatchGroups(FOverlappableTicks& OverlappableTick, FRHICommandList& RHICmdList, FDispatchGroupList& DispatchGroups, FEmitterInstanceList& InstancesWithPersistentIDs)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUDispatchSetup_RT);

	DispatchGroups.Reserve(8);
	FDispatchInstanceList EmittersWithStages;
	EmittersWithStages.Reserve(16);
	uint32 MaxStageCountForGroup = 0;

	//UE_LOG(LogNiagara, Warning, TEXT("NiagaraEmitterInstanceBatcher::BuildDispatchGroups:  %0xP"), this);
	for (FNiagaraGPUSystemTick* Tick : OverlappableTick)
	{
		const uint32 DispatchCount = Tick->Count;
		const bool bNeedsReset = Tick->bNeedsReset;

		int32 InstanceGroupIndex = 0;
		int32 PreviousInstanceFinalGroupIndex = 0;
		for (uint32 Index=0; Index < DispatchCount; ++Index)
		{
			FNiagaraComputeInstanceData& InstanceData = Tick->GetInstanceData()[Index];
			FNiagaraComputeExecutionContext* Context = InstanceData.Context;
			if ((Context == nullptr) || !Context->GPUScript_RT->IsShaderMapComplete_RenderThread())
			{
				continue;
			}

			// If we can't overlap the previous groups (i.e. particle read dependency) then we must start at the end to ensure all previous stages have completed
			if (DispatchGroups.IsValidIndex(InstanceGroupIndex) && (InstanceData.bStartNewOverlapGroup && DispatchGroups[InstanceGroupIndex].DispatchInstances.Num() > 0))
			{
				InstanceGroupIndex = PreviousInstanceFinalGroupIndex;
			}

			// Add persistent ID transitions
			const bool bRequiresPersistentIDs = Context->MainDataSet->RequiresPersistentIDs();

			FRHIUnorderedAccessView* FinalIDToIndexUAV = nullptr;

			int32 StageGroupIndex = InstanceGroupIndex;
			FDispatchInstance* FinalDispatchInstance = nullptr;
			for (uint32 iStage=0; iStage < Context->MaxUpdateIterations; ++iStage)
			{
				if (!NiagaraEmitterInstanceBatcherLocal::ShouldRunStage(Tick, Context, InstanceData.SimStageData[iStage].AlternateIterationSource, iStage))
				{
					continue;
				}

				FDispatchGroup* StageGroup = DispatchGroups.IsValidIndex(StageGroupIndex) ? &DispatchGroups[StageGroupIndex] : &DispatchGroups.AddDefaulted_GetRef();

				FNiagaraDataInterfaceProxyRW* IterationInterface = InstanceData.SimStageData[iStage].AlternateIterationSource;
				if (!IterationInterface)
				{
					if (FNiagaraDataBuffer* DestinationData = InstanceData.SimStageData[iStage].Destination)
					{
						AddDestinationBufferTransitions(StageGroup, DestinationData);

						if (bRequiresPersistentIDs)
						{
							// Insert a UAV barrier on the ID to index table, to prevent a race with the previous dispatch which also wrote to it.
							FRHIUnorderedAccessView* IDToIndexUAV = DestinationData->GetGPUIDToIndexTable().UAV;
							StageGroup->TransitionsBefore.Add(FRHITransitionInfo(IDToIndexUAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
							FinalIDToIndexUAV = IDToIndexUAV;
						}
					}

					// Make the free ID buffer readable.
					if (bRequiresPersistentIDs)
					{
						StageGroup->TransitionsBefore.Add(FRHITransitionInfo(Context->MainDataSet->GetGPUFreeIDs().UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
					}
				}

				// Add dispatch instances
				FinalDispatchInstance = &StageGroup->DispatchInstances.AddDefaulted_GetRef();
				FinalDispatchInstance->Tick = Tick;
				FinalDispatchInstance->InstanceData = &InstanceData;
				FinalDispatchInstance->StageIndex = iStage;

				++StageGroupIndex;
			}

			// Mark that this is the final stage run for this instance
			if (FinalDispatchInstance != nullptr)
			{
				FinalDispatchInstance->bFinalStage = true;
			}

			PreviousInstanceFinalGroupIndex = StageGroupIndex;

			if (FinalIDToIndexUAV)
			{
				InstancesWithPersistentIDs.Add(&InstanceData);
				DispatchGroups.Last().TransitionsAfter.Add(FRHITransitionInfo(FinalIDToIndexUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute));
			}

			// Final tick, if so enqueue a readback and set the data to render
			if (Tick->bIsFinalTick)
			{
				// If the emitter has stages, we'll call SetDataToRender() after we add all the stages, since those can flip the source/destination buffers a number of times.
				Context->SetDataToRender(Context->MainDataSet->GetCurrentData());
			}
		}
	}

	uint32 NumInstancesWithPersistentIDs = (uint32)InstancesWithPersistentIDs.Num();
	if (NumInstancesWithPersistentIDs > 0)
	{
		// These buffers will be needed by the simulation dispatches which come immediately after, so there will be a stall, but
		// moving this step to a different place is difficult, so we'll live with it for now.
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUClearIDTables);
		SCOPED_GPU_STAT(RHICmdList, NiagaraGPUClearIDTables);

		FNiagaraTransitionList IDToIndexTableTransitions;
		IDToIndexTableTransitions.SetNum(NumInstancesWithPersistentIDs);
		for (uint32 i = 0; i < NumInstancesWithPersistentIDs; ++i)
		{
			FNiagaraComputeInstanceData* Instance = InstancesWithPersistentIDs[i];
			FRHIUnorderedAccessView* IDToIndexUAV = Instance->SimStageData[0].Destination->GetGPUIDToIndexTable().UAV;
			IDToIndexTableTransitions[i] = FRHITransitionInfo(IDToIndexUAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute);
		}
		RHICmdList.Transition(IDToIndexTableTransitions);

		for (uint32 i = 0; i < NumInstancesWithPersistentIDs; ++i)
		{
			FNiagaraComputeInstanceData* Instance = InstancesWithPersistentIDs[i];
			SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUComputeClearIDToIndexBuffer, TEXT("Clear ID To Index Table - %s"), Instance->Context->GetDebugSimName());
			NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, Instance->SimStageData[0].Destination->GetGPUIDToIndexTable(), INDEX_NONE);
		}
	}
}

void NiagaraEmitterInstanceBatcher::DispatchAllOnCompute(FDispatchInstanceList& DispatchInstances, FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer)
{
	// Run all the PreStage functions in bulk.
	for (FDispatchInstance& DispatchInstance : DispatchInstances)
	{
		PreStageInterface(*DispatchInstance.Tick, DispatchInstance.InstanceData, RHICmdList, DispatchInstance.StageIndex);
	}

	// Run all the simulation compute shaders.
	for (FDispatchInstance& DispatchInstance : DispatchInstances)
	{
#if STATS
		if (GPUProfiler.IsProfilingEnabled() && DispatchInstance.Tick->bIsFinalTick)
		{
			FString StageName = "SpawnUpdate";
			if (DispatchInstance.StageIndex > 0)
			{
				for (FSimulationStageMetaData& MetaData : DispatchInstance.InstanceData->Context->SimStageInfo)
				{
					if (DispatchInstance.StageIndex >= MetaData.MinStage && DispatchInstance.StageIndex < MetaData.MaxStage)
					{
						StageName = MetaData.SimulationStageName.ToString();
					}
				}
			}
			TStatId StatId = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraDetailed>("GPU_Stage_" + StageName);
			int32 TimerHandle = GPUProfiler.StartTimer((uint64)DispatchInstance.InstanceData->Context, StatId, RHICmdList);
			DispatchStage(DispatchInstance, DispatchInstance.StageIndex, RHICmdList, ViewUniformBuffer);
			GPUProfiler.EndTimer(TimerHandle, RHICmdList);
		}
		else
		{
			DispatchStage(DispatchInstance, DispatchInstance.StageIndex, RHICmdList, ViewUniformBuffer);	
		}
#else
		DispatchStage(DispatchInstance, DispatchInstance.StageIndex, RHICmdList, ViewUniformBuffer);
#endif
	}

	// Run all the PostStage functions in bulk.
	for (FDispatchInstance& DispatchInstance : DispatchInstances)
	{
		PostStageInterface(*DispatchInstance.Tick, DispatchInstance.InstanceData, RHICmdList, DispatchInstance.StageIndex);
		if (DispatchInstance.bFinalStage)
		{
			PostSimulateInterface(*DispatchInstance.Tick, DispatchInstance.InstanceData, RHICmdList, DispatchInstance.InstanceData->Context->GPUScript_RT);
		}
	}
}

void NiagaraEmitterInstanceBatcher::ResizeFreeIDsListSizesBuffer(FRHICommandList& RHICmdList, uint32 NumInstances)
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
	RHICmdList.Transition(FRHITransitionInfo(FreeIDListSizesBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
	bFreeIDListSizesBufferCleared = false;
}

void NiagaraEmitterInstanceBatcher::ClearFreeIDsListSizesBuffer(FRHICommandList& RHICmdList)
{
	if (bFreeIDListSizesBufferCleared)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUComputeClearFreeIDListSizes);
	// The buffer has already been transitioned to UAVCompute.
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

	FNiagaraTransitionList Transitions;
	Transitions.Reserve(Instances.Num() + 1);
	// Insert a UAV barrier on the buffer, to make sure the previous clear finished writing to it.
	Transitions.Add(FRHITransitionInfo(FreeIDListSizesBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
	for(FNiagaraComputeInstanceData* Instance : Instances)
	{
		// The ID to index tables should already be in SRVCompute. Make the free ID tables writable.
		Transitions.Add(FRHITransitionInfo(Instance->Context->MainDataSet->GetGPUFreeIDs().UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute));
	}

	RHICmdList.Transition(Transitions);

	check((uint32)Instances.Num() <= NumAllocatedFreeIDListSizes);

	RHICmdList.BeginUAVOverlap(FreeIDListSizesBuffer.UAV);
	for (uint32 InstanceIdx = 0; InstanceIdx < (uint32)Instances.Num(); ++InstanceIdx)
	{
		FNiagaraComputeInstanceData* Instance = Instances[InstanceIdx];
		FNiagaraDataSet* MainDataSet = Instance->Context->MainDataSet;
		FNiagaraDataBuffer* FinalData = MainDataSet->GetCurrentData();

		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUComputeFreeIDsEmitter, TEXT("Update Free ID Buffer - %s"), Instance->Context->GetDebugSimName());
		NiagaraComputeGPUFreeIDs(RHICmdList, FeatureLevel, MainDataSet->GetGPUNumAllocatedIDs(), FinalData->GetGPUIDToIndexTable().SRV, MainDataSet->GetGPUFreeIDs(), FreeIDListSizesBuffer, InstanceIdx);
	}
	RHICmdList.EndUAVOverlap(FreeIDListSizesBuffer.UAV);

	bFreeIDListSizesBufferCleared = false;
}

void NiagaraEmitterInstanceBatcher::UpdateInstanceCountManager(FRHICommandListImmediate& RHICmdList)
{
	// Resize dispatch buffer count
	int32 TotalDispatchCount = 0;
	{
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
	}

	// Don't consume the readback data if we have no ticks as they will be gone forever
	// This causes an issue with sequencer ticks @ 30hz but the engine ticking @ 60hz
	// The readback really needs to be changed to not rely on the tick being enqueued, we should be pushing this data back
	if (TotalDispatchCount > 0)
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
}

void NiagaraEmitterInstanceBatcher::BuildTickStagePasses(FRHICommandListImmediate& RHICmdList, ETickStage GenerateTickStage)
{
	// Early out for bulk tick generation in PreInitViews
	const bool bGeneratePerTickStage = GNiagaraGpuLowLatencyTranslucencyEnabled == 0;
	if (!bGeneratePerTickStage && (GenerateTickStage != ETickStage::PreInitViews))
	{
		return;
	}

	for (int32 iTickStage = 0; iTickStage < (int)ETickStage::Max; ++iTickStage)
	{
		ContextsPerStage[iTickStage].Reset(Ticks_RT.Num());
		TicksPerStage[iTickStage].Reset(Ticks_RT.Num());
		CountsToRelease[iTickStage].Reset(Ticks_RT.Num());		//-TODO: Improve what we need here, should be able to make a better guess below
	}

	const bool bEnqueueReadback = !GPUInstanceCounterManager.HasPendingGPUReadback();

	for (FNiagaraGPUSystemTick& Tick : Ticks_RT)
	{
		FNiagaraComputeSharedContext* SharedContext = Tick.SharedContext;

		const ETickStage TickStage = NiagaraEmitterInstanceBatcherLocal::CalculateTickStage(Tick);
		if (bGeneratePerTickStage && (GenerateTickStage != TickStage))
		{
			continue;
		}

		BuildConstantBuffers(Tick);

		Tick.bIsFinalTick = false;

		const bool bResetCounts = SharedContext->ScratchIndex == INDEX_NONE;
		if (bResetCounts)
		{
			check(!ContextsPerStage[(int)ETickStage::PreInitViews].Contains(SharedContext));
			check(!ContextsPerStage[(int)ETickStage::PostInitViews].Contains(SharedContext));
			check(!ContextsPerStage[(int)ETickStage::PostOpaqueRender].Contains(SharedContext));
			ContextsPerStage[(int)TickStage].Add(SharedContext);
			SharedContext->ScratchTickStage = (int)TickStage;
		}

		// Here scratch index represent the index of the last tick
		SharedContext->ScratchIndex = TicksPerStage[(int)TickStage].Add(&Tick);
		check(SharedContext->ScratchTickStage == (int)TickStage);

		// Allows us to count total required instances across all ticks
		for (uint32 i = 0; i < Tick.Count; ++i)
		{
			FNiagaraComputeInstanceData& InstanceData = Tick.GetInstanceData()[i];
			FNiagaraComputeExecutionContext* ExecContext = InstanceData.Context;
			if ((ExecContext == nullptr) || !ExecContext->GPUScript_RT->IsShaderMapComplete_RenderThread())
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

			InstanceData.SimStageData[0].SourceNumInstances = PrevNumInstances;
			InstanceData.SimStageData[0].DestinationNumInstances = ExecContext->ScratchNumInstances;
		}
	}

	for (int32 iTickStage = 0; iTickStage < (int)ETickStage::Max; ++iTickStage)
	{
		// Set bIsFinalTick for the last tick of each context and reset the scratch index.
		const int32 ScrachIndexReset = UseOverlapCompute() ? 0 : INDEX_NONE;
		for (FNiagaraComputeSharedContext* SharedContext : ContextsPerStage[iTickStage])
		{
			TicksPerStage[iTickStage][SharedContext->ScratchIndex]->bIsFinalTick = true;
			SharedContext->ScratchIndex = ScrachIndexReset;
		}

		for (FNiagaraGPUSystemTick* Tick : TicksPerStage[iTickStage])
		{
			for (uint32 i = 0; i < Tick->Count; ++i)
			{
				FNiagaraComputeInstanceData& InstanceData = Tick->GetInstanceData()[i];
				FNiagaraComputeExecutionContext* ExecContext = InstanceData.Context;
				if ((ExecContext == nullptr) || !ExecContext->GPUScript_RT->IsShaderMapComplete_RenderThread())
				{
					continue;
				}

				// First stage is presumed to be a particle stage
				FNiagaraDataBuffer* CurrentData = ExecContext->MainDataSet->GetCurrentData();
				FNiagaraDataBuffer* DestinationData = &ExecContext->MainDataSet->BeginSimulate();

				if (ExecContext->MainDataSet->RequiresPersistentIDs())
				{
					ExecContext->MainDataSet->AllocateGPUFreeIDs(ExecContext->ScratchMaxInstances + 1, RHICmdList, FeatureLevel, ExecContext->GetDebugSimName());
				}

				if (DestinationData->GetGPUInstanceCountBufferOffset() != INDEX_NONE)
				{
					CountsToRelease[iTickStage].Add(DestinationData->GetGPUInstanceCountBufferOffset());
					DestinationData->ClearGPUInstanceCountBufferOffset();
				}

				DestinationData->AllocateGPU(ExecContext->ScratchMaxInstances + 1, GPUInstanceCounterManager, RHICmdList, FeatureLevel, ExecContext->GetDebugSimName());

				InstanceData.SimStageData[0].Source = CurrentData;
				InstanceData.SimStageData[0].SourceCountOffset = CurrentData->GetGPUInstanceCountBufferOffset();
				InstanceData.SimStageData[0].Destination = DestinationData;
				InstanceData.SimStageData[0].DestinationCountOffset = DestinationData->GetGPUInstanceCountBufferOffset();

				ExecContext->MainDataSet->EndSimulate();

				// If we are going to enqueue a readback this frame
				//-TODO: Currently this does not work with low latency translucency, we need to allocate counters and release / modify DataBuffers when executing to avoid issues around renders picking up the wrong ones that are opaque
				bool bClearBufferFromReadback = false;
				if (bEnqueueReadback && Tick->bIsFinalTick && (ExecContext->EmitterInstanceReadback.GPUCountOffset == INDEX_NONE) && (CurrentData->GetGPUInstanceCountBufferOffset() != INDEX_NONE))
				{
					bClearBufferFromReadback = true;
					ExecContext->EmitterInstanceReadback.GPUCountOffset = InstanceData.SimStageData[0].Source->GetGPUInstanceCountBufferOffset();
					ExecContext->EmitterInstanceReadback.CPUCount = InstanceData.SimStageData[0].SourceNumInstances;
				}

				if (Tick->NumInstancesWithSimStages > 0)
				{
					// Setup iteration source for stage 0
					InstanceData.SimStageData[0].AlternateIterationSource = FindIterationInterface(&InstanceData, 0);

					const uint32 NumStages = InstanceData.Context->MaxUpdateIterations;
					if (NumStages > 1)
					{
						// Flip current / destination buffers so we read from the buffer we just wrote into on the next stage
						Swap(CurrentData, DestinationData);

						uint32 CurrentNumInstances = InstanceData.SimStageData[0].DestinationNumInstances;
						uint32 DestinationNumInstances = InstanceData.SimStageData[0].SourceNumInstances;

						// Old simulation will always RW the instance count, so ensure we have a count present for the destination
						if (InstanceData.bUsesOldShaderStages)
						{
							if (DestinationData->GetGPUInstanceCountBufferOffset() == INDEX_NONE)
							{
								DestinationData->AllocateGPU(ExecContext->ScratchMaxInstances + 1, GPUInstanceCounterManager, RHICmdList, FeatureLevel, ExecContext->GetDebugSimName());
							}
						}


						for (uint32 SimulationStageIndex = 1; SimulationStageIndex < NumStages; ++SimulationStageIndex)
						{
							InstanceData.SimStageData[SimulationStageIndex].Source = CurrentData;
							InstanceData.SimStageData[SimulationStageIndex].SourceCountOffset = CurrentData->GetGPUInstanceCountBufferOffset();
							InstanceData.SimStageData[SimulationStageIndex].SourceNumInstances = CurrentNumInstances;
							InstanceData.SimStageData[SimulationStageIndex].Destination = DestinationData;
							InstanceData.SimStageData[SimulationStageIndex].DestinationCountOffset = InstanceData.bUsesOldShaderStages ? DestinationData->GetGPUInstanceCountBufferOffset() : INDEX_NONE;
							InstanceData.SimStageData[SimulationStageIndex].DestinationNumInstances = DestinationNumInstances;

							// Determine if the iteration is outputting to a custom data size
							FNiagaraDataInterfaceProxyRW* IterationInterface = FindIterationInterface(&InstanceData, SimulationStageIndex);
							InstanceData.SimStageData[SimulationStageIndex].AlternateIterationSource = IterationInterface;

							if (!NiagaraEmitterInstanceBatcherLocal::ShouldRunStage(Tick, ExecContext, IterationInterface, SimulationStageIndex))
							{
								continue;
							}

							if (!InstanceData.bUsesOldShaderStages)
							{
								// This should never be nullptr with simulation stages
								const FSimulationStageMetaData* StageMetaData = ExecContext->GetSimStageMetaData(SimulationStageIndex);
								check(StageMetaData);
								InstanceData.SimStageData[SimulationStageIndex].StageMetaData = StageMetaData;

								// No particle data will be written we read only, i.e. scattering particles into a grid
								if (!StageMetaData->bWritesParticles)
								{
									InstanceData.SimStageData[SimulationStageIndex].Destination = nullptr;
									InstanceData.SimStageData[SimulationStageIndex].DestinationNumInstances = CurrentNumInstances;
									continue;
								}

								// Particle counts are not changing and we can safely read / write to the same particle buffer
								if (StageMetaData->bPartialParticleUpdate)
								{
									InstanceData.SimStageData[SimulationStageIndex].Source = nullptr;

									InstanceData.SimStageData[SimulationStageIndex].Destination = CurrentData;
									InstanceData.SimStageData[SimulationStageIndex].DestinationCountOffset = INDEX_NONE;
									InstanceData.SimStageData[SimulationStageIndex].DestinationNumInstances = CurrentNumInstances;
									continue;
								}
							}

							// We need to allocate a new buffer as particle counts could be changing
							ensure(CurrentData == ExecContext->MainDataSet->GetCurrentData());
							DestinationData = &ExecContext->MainDataSet->BeginSimulate(false);
							if (DestinationData->GetGPUInstanceCountBufferOffset() != INDEX_NONE)
							{
								if (bClearBufferFromReadback && (ExecContext->EmitterInstanceReadback.GPUCountOffset == DestinationData->GetGPUInstanceCountBufferOffset()))
								{
									bClearBufferFromReadback = false;
								}
								else
								{
									CountsToRelease[iTickStage].Add(DestinationData->GetGPUInstanceCountBufferOffset());
								}
								DestinationData->ClearGPUInstanceCountBufferOffset();
							}
							DestinationData->AllocateGPU(ExecContext->ScratchMaxInstances + 1, GPUInstanceCounterManager, RHICmdList, FeatureLevel, ExecContext->GetDebugSimName());
							ExecContext->MainDataSet->EndSimulate();

							DestinationNumInstances = CurrentNumInstances;

							InstanceData.SimStageData[SimulationStageIndex].Destination = DestinationData;
							InstanceData.SimStageData[SimulationStageIndex].DestinationCountOffset = DestinationData->GetGPUInstanceCountBufferOffset();
							InstanceData.SimStageData[SimulationStageIndex].DestinationNumInstances = DestinationNumInstances;

							Swap(CurrentData, DestinationData);
						}
					}
				}

				// If we queued an emitter readback we need to clear out the count from the original buffer to avoid it being released
				if (bClearBufferFromReadback)
				{
					check(InstanceData.SimStageData[0].Source && (InstanceData.SimStageData[0].Source->GetGPUInstanceCountBufferOffset() != INDEX_NONE));
					InstanceData.SimStageData[0].Source->ClearGPUInstanceCountBufferOffset();
				}

				if (Tick->bIsFinalTick && GNiagaraGpuLowLatencyTranslucencyEnabled)
				{
					ExecContext->SetTranslucentDataToRender(ExecContext->MainDataSet->GetCurrentData());
				}
			}
		}
	}
}

void NiagaraEmitterInstanceBatcher::ExecuteAll(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, ETickStage TickStage)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUSimTick_RT);

	// Anything to execute for this tick stage?
	if (TicksPerStage[(int)TickStage].Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraEmitterInstanceBatcher_ExecuteAll, TEXT("NiagaraEmitterInstanceBatcher_ExecuteAll - TickStage(%d)"), TickStage);

	FUniformBufferStaticBindings GlobalUniformBuffers;
	if (FRHIUniformBuffer* SceneTexturesUniformBuffer = GNiagaraViewDataManager.GetSceneTextureUniformParameters())
	{
		GlobalUniformBuffers.AddUniformBuffer(SceneTexturesUniformBuffer);
	}
	SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

	FMemMark Mark(FMemStack::Get());
	TArray<FOverlappableTicks, TMemStackAllocator<> > SimPasses;
	{
		if (UseOverlapCompute())
		{
			// Transpose now only once the data to get all independent tick per pass
			SimPasses.Reserve(2); // Safe bet!

			for (FNiagaraGPUSystemTick* Tick : TicksPerStage[(int)TickStage])
			{
				FNiagaraComputeSharedContext* SharedContext = Tick->SharedContext;
				const int32 ScratchIndex = SharedContext->ScratchIndex;
				check(ScratchIndex != INDEX_NONE);

				if (ScratchIndex >= SimPasses.Num())
				{
					SimPasses.AddDefaulted(SimPasses.Num() - ScratchIndex + 1);
					if (ScratchIndex == 0)
					{
						SimPasses[0].Reserve(ContextsPerStage[(int)TickStage].Num()); // Guarantied!
					}
				}
				SimPasses[ScratchIndex].Add(Tick);
				// Scratch index is now the number of passes for this context.
				if (Tick->bIsFinalTick)
				{
					// Reset to default as it will no longer be used.
					SharedContext->ScratchIndex = INDEX_NONE;
				}
				else
				{
					SharedContext->ScratchIndex += 1;
				}
			}
		}
		else
		{
			// Force dispatches to run individually, this should only be used for debugging as it is highly inefficient on the GPU
			SimPasses.Reserve(ContextsPerStage[(int)TickStage].Num()); // Guarantied!
			for (FNiagaraGPUSystemTick* Tick : TicksPerStage[(int)TickStage])
			{
				SimPasses.AddDefaulted_GetRef().Add(Tick);
			}
		}
	}

	if (SimPasses.Num() > 0)
	{
		FEmitterInstanceList InstancesWithPersistentIDs;
		for (int32 SimPassIdx = 0; SimPassIdx < SimPasses.Num(); ++SimPassIdx)
		{
			FOverlappableTicks& SimPass = SimPasses[SimPassIdx];
			InstancesWithPersistentIDs.SetNum(0, false);

			FDispatchGroupList DispatchGroups;
			BuildDispatchGroups(SimPass, RHICmdList, DispatchGroups, InstancesWithPersistentIDs);
			const bool bHasInstancesWithPersistentIDs = (InstancesWithPersistentIDs.Num() > 0);

			{
				SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUSimulation);
				SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSimulation);
				for (int32 GroupIdx = 0; GroupIdx < DispatchGroups.Num(); ++GroupIdx)
				{
					FDispatchGroup& Group = DispatchGroups[GroupIdx];
					//SCOPED_DRAW_EVENTF(RHICmdList, NiagaraOverlapGroup, TEXT("Overlap Group - %u emitters"), Group.DispatchInstances.Num());

					const bool bIsFirstGroup = (GroupIdx == 0);
					const bool bIsLastGroup = (GroupIdx == DispatchGroups.Num() - 1);

					// If we're the first group, insert a transition from readable to writable on the instance count buffer. Otherwise we need a UAV to UAV barrier.
					ERHIAccess InstanceCountBufferSrcAccess = bIsFirstGroup ? FNiagaraGPUInstanceCountManager::kCountBufferDefaultState : ERHIAccess::UAVCompute;
					Group.TransitionsBefore.Add(FRHITransitionInfo(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV, InstanceCountBufferSrcAccess, ERHIAccess::UAVCompute));

					// If we're the first group and the FreeIDListSizesBuffer is in use, transition it here, so the clear we do at the end can overlap the simulation dispatches.
					if (bIsFirstGroup && bHasInstancesWithPersistentIDs && FreeIDListSizesBuffer.Buffer)
					{
						Group.TransitionsBefore.Add(FRHITransitionInfo(FreeIDListSizesBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
					}

					// If we're the first group and the FreeIDListSizesBuffer is in use, transition it here, so the clear we do at the end can overlap the simulation dispatches.
					if (bIsFirstGroup && bHasInstancesWithPersistentIDs && FreeIDListSizesBuffer.Buffer)
					{
						Group.TransitionsBefore.Add(FRHITransitionInfo(FreeIDListSizesBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute));
					}

					// If we're the last group, make the instance count buffer readable after the dispatch.
					if (bIsLastGroup)
					{
						Group.TransitionsAfter.Add(FRHITransitionInfo(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV, ERHIAccess::UAVCompute, FNiagaraGPUInstanceCountManager::kCountBufferDefaultState));
					}

					RHICmdList.Transition(Group.TransitionsBefore);

					RHICmdList.BeginUAVOverlap(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
					DispatchAllOnCompute(Group.DispatchInstances, RHICmdList, ViewUniformBuffer);
					RHICmdList.EndUAVOverlap(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);

					// If we're the last group and we have emitters with persistent IDs, clear the ID list sizes before doing the final transitions, to overlap that dispatch with the simulation dispatches.
					if (bIsLastGroup && bHasInstancesWithPersistentIDs)
					{
						// If we're doing multiple ticks (e.g. when scrubbing the timeline in the editor), we must update the free ID buffers before running
						// the next tick, which will cause stalls (because the ID to index buffer is written by DispatchAllOnCompute and read by UpdateFreeIDBuffers).
						// However, when we're at the last tick, we can postpone the update until later in the frame and avoid the stall. This will be the case when
						// running normally, with one tick per frame.
						if (SimPassIdx < SimPasses.Num() - 1)
						{
							ResizeFreeIDsListSizesBuffer(RHICmdList, InstancesWithPersistentIDs.Num());
							ClearFreeIDsListSizesBuffer(RHICmdList);
							UpdateFreeIDBuffers(RHICmdList, InstancesWithPersistentIDs);
						}
						else
						{
							DeferredIDBufferUpdates.Append(InstancesWithPersistentIDs);
							ResizeFreeIDsListSizesBuffer(RHICmdList, DeferredIDBufferUpdates.Num());

							// Speculatively clear the list sizes buffer here. Under normal circumstances, this happens in the first stage which finds instances with persistent IDs
							// (usually PreInitViews) and it's finished by the time the deferred updates need to be processed. If a subsequent tick stage runs multiple time ticks,
							// the first step will find the buffer already cleared and will not clear again. The only time when this clear is superfluous is when a following stage
							// reallocates the buffer, but that's unlikely (and amortized) because we allocate in chunks.
							ClearFreeIDsListSizesBuffer(RHICmdList);
						}
					}

					RHICmdList.Transition(Group.TransitionsAfter);
				}
			}
		}

	}

	// Release counts
	GPUInstanceCounterManager.FreeEntryArray(MakeArrayView(CountsToRelease[int(TickStage)]));
	CountsToRelease[int(TickStage)].Reset();
}

void NiagaraEmitterInstanceBatcher::PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate)
{
	FramesBeforeTickFlush = 0;

	GpuReadbackManagerPtr->Tick();
#if NIAGARA_COMPUTEDEBUG_ENABLED
	if ( FNiagaraGpuComputeDebug* GpuComputeDebug = GetGpuComputeDebug() )
	{
		GpuComputeDebug->Tick(RHICmdList);
	}
#endif


	if (!FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		FinishDispatches();
		return;
	}

#if STATS
	// check if we can process profiling results from previous frames
	if (GPUProfiler.IsProfilingEnabled())
	{
		TArray<FNiagaraGPUTimingResult, TInlineAllocator<16>> ProfilingResults;
		GPUProfiler.QueryTimingResults(RHICmdList, ProfilingResults);
		TMap<TWeakObjectPtr<UNiagaraEmitter>, TMap<TStatIdData const*, float>> CapturedStats;
		for (const FNiagaraGPUTimingResult& Result : ProfilingResults)
		{
			// map the individual timings to the emitters and source scripts
			if (Result.ReporterHandle)
			{
				FNiagaraComputeExecutionContext* Context = (FNiagaraComputeExecutionContext*)Result.ReporterHandle;
				if (Context->EmitterPtr.IsValid())
				{
					CapturedStats.FindOrAdd(Context->EmitterPtr).FindOrAdd(Result.StatId.GetRawPointer()) += Result.ElapsedMicroseconds;
				}
			}
		}
		if (CapturedStats.Num() > 0)
		{
			// report the captured gpu stats on the game thread
			uint64 ReporterHandle = (uint64)this;
			AsyncTask(ENamedThreads::GameThread, [LocalCapturedStats = MoveTemp(CapturedStats), ReporterHandle]
            {
                for (auto& Entry : LocalCapturedStats)
                {
                    if (Entry.Key.IsValid())
                    {
                        Entry.Key->GetStatData().AddStatCapture(TTuple<uint64, ENiagaraScriptUsage>(ReporterHandle, ENiagaraScriptUsage::ParticleGPUComputeScript), Entry.Value);
                    }
                }
            });
		}
	}
#endif
	
	LLM_SCOPE(ELLMTag::Niagara);
	TotalDispatchesThisFrame = 0;

	// Reset the list of GPUSort tasks and release any resources they hold on to.
	// It might be worth considering doing so at the end of the render to free the resources immediately.
	// (note that currently there are no callback appropriate to do it)
	SimulationsToSort.Reset();

	// Update draw indirect buffer to max possible size.
	if (bAllowGPUParticleUpdate)
	{
		UpdateInstanceCountManager(RHICmdList);
		BuildTickStagePasses(RHICmdList, ETickStage::PreInitViews);

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
		BuildTickStagePasses(RHICmdList, ETickStage::PostInitViews);
		ExecuteAll(RHICmdList, ViewUniformBuffer, ETickStage::PostInitViews);
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
		BuildTickStagePasses(RHICmdList, ETickStage::PostOpaqueRender);

		// Setup new readback since if there is no pending request, there is no risk of having invalid data read (offset being allocated after the readback was sent).
		ExecuteAll(RHICmdList, ViewUniformBuffer, ETickStage::PostOpaqueRender);

		UpdateFreeIDBuffers(RHICmdList, DeferredIDBufferUpdates);
		DeferredIDBufferUpdates.SetNum(0, false);

		FinishDispatches();

		ProcessDebugReadbacks(RHICmdList, false);
	}

	if (!GPUInstanceCounterManager.HasPendingGPUReadback())
	{
		GPUInstanceCounterManager.EnqueueGPUReadback(RHICmdList);
	}
}

void NiagaraEmitterInstanceBatcher::ProcessDebugReadbacks(FRHICommandListImmediate& RHICmdList, bool bWaitCompletion)
{
#if WITH_EDITOR
	// Execute any pending readbacks as the ticks have now all been processed
	for (const FDebugReadbackInfo& DebugReadback : GpuDebugReadbackInfos)
	{
		FNiagaraDataBuffer* CurrentDataBuffer = DebugReadback.Context->MainDataSet->GetCurrentData();
		if ( CurrentDataBuffer == nullptr )
		{
			// Data is invalid
			DebugReadback.DebugInfo->Frame.CopyFromGPUReadback(nullptr, nullptr, nullptr, 0, 0, 0, 0, 0);
			DebugReadback.DebugInfo->bWritten = true;
			continue;
		}

		const uint32 CountOffset = CurrentDataBuffer->GetGPUInstanceCountBufferOffset();
		if (CountOffset == INDEX_NONE)
		{
			// Data is invalid
			DebugReadback.DebugInfo->Frame.CopyFromGPUReadback(nullptr, nullptr, nullptr, 0, 0, 0, 0, 0);
			DebugReadback.DebugInfo->bWritten = true;
			continue;
		}

		// Execute readback
		constexpr int32 MaxReadbackBuffers = 4;
		TArray<FRHIVertexBuffer*, TInlineAllocator<MaxReadbackBuffers>> ReadbackBuffers;

		const int32 CountBufferIndex = ReadbackBuffers.Add(GPUInstanceCounterManager.GetInstanceCountBuffer().Buffer);
		const int32 FloatBufferIndex = (CurrentDataBuffer->GetGPUBufferFloat().NumBytes == 0) ? INDEX_NONE : ReadbackBuffers.Add(CurrentDataBuffer->GetGPUBufferFloat().Buffer);
		const int32 HalfBufferIndex = (CurrentDataBuffer->GetGPUBufferHalf().NumBytes == 0) ? INDEX_NONE : ReadbackBuffers.Add(CurrentDataBuffer->GetGPUBufferHalf().Buffer);
		const int32 IntBufferIndex = (CurrentDataBuffer->GetGPUBufferInt().NumBytes == 0) ? INDEX_NONE : ReadbackBuffers.Add(CurrentDataBuffer->GetGPUBufferInt().Buffer);

		const int32 FloatBufferStride = CurrentDataBuffer->GetFloatStride();
		const int32 HalfBufferStride = CurrentDataBuffer->GetHalfStride();
		const int32 IntBufferStride = CurrentDataBuffer->GetInt32Stride();

		GpuReadbackManagerPtr->EnqueueReadbacks(
			RHICmdList,
			MakeArrayView(ReadbackBuffers),
			[=, DebugInfo=DebugReadback.DebugInfo](TConstArrayView<TPair<void*, uint32>> BufferData)
			{
				checkf(4 + (CountOffset * 4) <= BufferData[CountBufferIndex].Value, TEXT("CountOffset %d is out of bounds"), CountOffset, BufferData[CountBufferIndex].Value);
				const int32 InstanceCount = reinterpret_cast<int32*>(BufferData[CountBufferIndex].Key)[CountOffset];
				const float* FloatDataBuffer = FloatBufferIndex == INDEX_NONE ? nullptr : reinterpret_cast<float*>(BufferData[FloatBufferIndex].Key);
				const FFloat16* HalfDataBuffer = HalfBufferIndex == INDEX_NONE ? nullptr : reinterpret_cast<FFloat16*>(BufferData[HalfBufferIndex].Key);
				const int32* IntDataBuffer = IntBufferIndex == INDEX_NONE ? nullptr : reinterpret_cast<int32*>(BufferData[IntBufferIndex].Key);

				DebugInfo->Frame.CopyFromGPUReadback(FloatDataBuffer, IntDataBuffer, HalfDataBuffer, 0, InstanceCount, FloatBufferStride, IntBufferStride, HalfBufferStride);
				DebugInfo->bWritten = true;
			}
		);
	}

	if (bWaitCompletion)
	{
		GpuReadbackManagerPtr->WaitCompletion(RHICmdList);
	}
#endif
}

bool NiagaraEmitterInstanceBatcher::UsesGlobalDistanceField() const
{
	checkSlow(Ticks_RT.ContainsByPredicate([](const FNiagaraGPUSystemTick& Tick) { return Tick.bRequiresDistanceFieldData; }) == NumTicksThatRequireDistanceFieldData > 0);

	return NumTicksThatRequireDistanceFieldData > 0;
}

bool NiagaraEmitterInstanceBatcher::UsesDepthBuffer() const
{
	checkSlow(Ticks_RT.ContainsByPredicate([](const FNiagaraGPUSystemTick& Tick) { return Tick.bRequiresDepthBuffer; }) == NumTicksThatRequireDepthBuffer > 0);

	return NumTicksThatRequireDepthBuffer > 0;
}

bool NiagaraEmitterInstanceBatcher::RequiresEarlyViewUniformBuffer() const
{
	checkSlow(Ticks_RT.ContainsByPredicate([](const FNiagaraGPUSystemTick& Tick) { return Tick.bRequiresEarlyViewData; }) == NumTicksThatRequireEarlyViewData > 0);

	return NumTicksThatRequireEarlyViewData > 0;
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

	const FGPUSortManager::FKeyGenInfo KeyGenInfo((uint32)NumElementsInBatch, EnumHasAnyFlags(Flags, EGPUSortFlags::HighPrecisionKeys));

	FNiagaraSortKeyGenCS::FPermutationDomain SortPermutationVector;
	SortPermutationVector.Set<FNiagaraSortKeyGenCS::FSortUsingMaxPrecision>(GNiagaraGPUSortingUseMaxPrecision != 0);
	SortPermutationVector.Set<FNiagaraSortKeyGenCS::FEnableCulling>(false);

	FNiagaraSortKeyGenCS::FPermutationDomain SortAndCullPermutationVector;
	SortAndCullPermutationVector.Set<FNiagaraSortKeyGenCS::FSortUsingMaxPrecision>(GNiagaraGPUSortingUseMaxPrecision != 0);
	SortAndCullPermutationVector.Set<FNiagaraSortKeyGenCS::FEnableCulling>(true);

	TShaderMapRef<FNiagaraSortKeyGenCS> SortKeyGenCS(GetGlobalShaderMap(FeatureLevel), SortPermutationVector);
	TShaderMapRef<FNiagaraSortKeyGenCS> SortAndCullKeyGenCS(GetGlobalShaderMap(FeatureLevel), SortAndCullPermutationVector);

	FRWBuffer* CulledCountsBuffer = GPUInstanceCounterManager.AcquireCulledCountsBuffer(RHICmdList, FeatureLevel);

	FNiagaraSortKeyGenCS::FParameters Params;
	Params.SortKeyMask = KeyGenInfo.SortKeyParams.X;
	Params.SortKeyShift = KeyGenInfo.SortKeyParams.Y;
	Params.SortKeySignBit = KeyGenInfo.SortKeyParams.Z;
	Params.OutKeys = KeysUAV;
	Params.OutParticleIndices = ValuesUAV;

	FRHIUnorderedAccessView* OverlapUAVs[3];
	uint32 NumOverlapUAVs = 0;

	OverlapUAVs[NumOverlapUAVs] = KeysUAV;
	++NumOverlapUAVs;
	OverlapUAVs[NumOverlapUAVs] = ValuesUAV;
	++NumOverlapUAVs;

	if (CulledCountsBuffer)
	{
		Params.OutCulledParticleCounts = CulledCountsBuffer->UAV;
		OverlapUAVs[NumOverlapUAVs] = CulledCountsBuffer->UAV;
		++NumOverlapUAVs;
	}
	else
	{
		Params.OutCulledParticleCounts = GetEmptyRWBufferFromPool(RHICmdList, PF_R32_UINT);
	}

	RHICmdList.BeginUAVOverlap(MakeArrayView(OverlapUAVs, NumOverlapUAVs));

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
			Params.CullingWorldSpaceOffset = SortInfo.CullingWorldSpaceOffset;

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
		}
	}

	RHICmdList.EndUAVOverlap(MakeArrayView(OverlapUAVs, NumOverlapUAVs));
}

/* Set shader parameters for data interfaces
 */
void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList& RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 SimulationStageIndex) const
{
	// set up data interface buffers, as defined by the DIs during compilation
	//

	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FNiagaraSystemInstanceID& SystemInstanceID = Tick.SystemInstanceID;
	const FNiagaraShaderMapPointerTable& PointerTable = Shader.GetPointerTable();

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			FNiagaraDataInterfaceSetArgs Context(Interface, SystemInstanceID, this, Shader, Instance, SimulationStageIndex, Instance->IsOutputStage(Interface, SimulationStageIndex), Instance->IsIterationStage(Interface, SimulationStageIndex));
			DIParam.DIType.Get(PointerTable.DITypes)->SetParameters(DIParam.Parameters.Get(), RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::UnsetDataInterfaceParameters(const TArray<FNiagaraDataInterfaceProxy*> &DataInterfaceProxies, const FNiagaraShaderRef& Shader, FRHICommandList &RHICmdList, const FNiagaraComputeInstanceData* Instance, const FNiagaraGPUSystemTick& Tick, uint32 SimulationStageIndex) const
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
			FNiagaraDataInterfaceSetArgs Context(Interface, SystemInstance, this, Shader, Instance, SimulationStageIndex, Instance->IsOutputStage(Interface, SimulationStageIndex), Instance->IsIterationStage(Interface, SimulationStageIndex));
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
	FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const FNiagaraGpuSpawnInfo& SpawnInfo, bool bCopyBeforeStart, uint32 DefaultSimulationStageIndex, uint32 SimulationStageIndex, FNiagaraDataInterfaceProxyRW* IterationInterface, bool HasRunParticleStage)
{
	FNiagaraComputeExecutionContext* Context = Instance->Context;

	// We must always set the current counts to ensure they are up to date when we have 0 instances
	FNiagaraDataBuffer* CurrentData = Instance->SimStageData[SimulationStageIndex].Source;
	if (CurrentData != nullptr)
	{
		CurrentData->SetNumInstances(Instance->SimStageData[SimulationStageIndex].SourceNumInstances);
	}

	FNiagaraDataBuffer* DestinationData = Instance->SimStageData[SimulationStageIndex].Destination;
	int32 InstancesToSpawnThisFrame = 0;
	if (DestinationData)
	{
		DestinationData->SetNumInstances(Instance->SimStageData[SimulationStageIndex].DestinationNumInstances);
		DestinationData->SetIDAcquireTag(FNiagaraComputeExecutionContext::TickCounter);

		// Only spawn particles on the first stage
		if (!HasRunParticleStage)
		{
			if (Instance->SimStageData[SimulationStageIndex].DestinationNumInstances > Instance->SimStageData[SimulationStageIndex].SourceNumInstances)
			{
				InstancesToSpawnThisFrame = Instance->SimStageData[SimulationStageIndex].DestinationNumInstances - Instance->SimStageData[SimulationStageIndex].SourceNumInstances;
			}
		}
		DestinationData->SetNumSpawnedInstances(InstancesToSpawnThisFrame);
	}

	if (TotalNumInstances == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS, TEXT("NiagaraGpuSim(%s) NumInstances(%u) Stage(%s %u) NumInstructions(%u)"),
		Context->GetDebugSimName(),
		TotalNumInstances,
		Context->GetSimStageMetaData(SimulationStageIndex) ? *Context->GetSimStageMetaData(SimulationStageIndex)->SimulationStageName.ToString() : TEXT("Particles"),
		SimulationStageIndex,
		Shader->GetNumInstructions()
	);

	const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies = Instance->DataInterfaceProxies;

	FRHIComputeShader* ComputeShader = Shader.GetComputeShader();
	RHICmdList.SetComputeShader(ComputeShader);

	// #todo(dmp): clean up this logic for shader stages on first frame
	SetShaderValue(RHICmdList, ComputeShader, Shader->SimStartParam, Tick.bNeedsReset ? 1U : 0U);

	// set the view uniform buffer param
	if (Shader->ViewUniformBufferParam.IsBound())
	{
		if (ensureMsgf(ViewUniformBuffer != nullptr, TEXT("ViewUniformBuffer is required for '%s' but we do not have one to bind"), Context->GetDebugSimName()))
		{
			RHICmdList.SetShaderUniformBuffer(ComputeShader, Shader->ViewUniformBufferParam.GetBaseIndex(), ViewUniformBuffer);
		}
	}

	SetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick, SimulationStageIndex);

	// set the shader and data set params 
	//
	const bool bRequiresPersistentIDs = Context->MainDataSet->RequiresPersistentIDs();
	SetSRVParameter(RHICmdList, Shader.GetComputeShader(), Shader->FreeIDBufferParam, bRequiresPersistentIDs ? Context->MainDataSet->GetGPUFreeIDs().SRV.GetReference() : FNiagaraRenderer::GetDummyIntBuffer());

	FNiagaraDataBuffer::SetInputShaderParams(RHICmdList, Shader.GetShader(), CurrentData);
	FNiagaraDataBuffer::SetOutputShaderParams(RHICmdList, Shader.GetShader(), DestinationData);

	// set the instance count uav
	//
	if (Shader->InstanceCountsParam.IsBound())
	{
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

	{
		// Packed data where
		// X = Count Buffer Instance Count Offset (INDEX_NONE == Use Instance Count)
		// Y = Instance Count
		// Z = Iteration Index
		// W = Num Iterations
		FIntVector4 SimulationStageIterationInfo = { INDEX_NONE, -1, 0, 0 };
		float SimulationStageNormalizedIterationIndex = 0.0f;

		if (IterationInterface)
		{
			const uint32 IterationInstanceCountOffset = IterationInterface->GetGPUInstanceCountOffset(Tick.SystemInstanceID);
			SimulationStageIterationInfo.X = IterationInstanceCountOffset;
			SimulationStageIterationInfo.Y = IterationInstanceCountOffset == INDEX_NONE ? TotalNumInstances : 0;
			if (const FSimulationStageMetaData* StageMetaData = Instance->SimStageData[SimulationStageIndex].StageMetaData)
			{
				const int32 NumStages = StageMetaData->MaxStage - StageMetaData->MinStage;
				ensure((int32(SimulationStageIndex) >= StageMetaData->MinStage) && (int32(SimulationStageIndex) < StageMetaData->MaxStage));
				const int32 IterationIndex = SimulationStageIndex - StageMetaData->MinStage;
				SimulationStageIterationInfo.Z = SimulationStageIndex - StageMetaData->MinStage;
				SimulationStageIterationInfo.W = NumStages;
				SimulationStageNormalizedIterationIndex = NumStages > 1 ? float(IterationIndex) / float(NumStages - 1) : 1.0f;
			}
		}
		SetShaderValue(RHICmdList, ComputeShader, Shader->SimulationStageIterationInfoParam, SimulationStageIterationInfo);
		SetShaderValue(RHICmdList, ComputeShader, Shader->SimulationStageNormalizedIterationIndexParam, SimulationStageNormalizedIterationIndex);
	}

	// Execute the dispatch
	{
		const uint32 ThreadGroupSize = FNiagaraShader::GetGroupSize(ShaderPlatform);
		checkf(ThreadGroupSize <= NiagaraComputeMaxThreadGroupSize, TEXT("ShaderPlatform(%d) is set to ThreadGroup size (%d) which is > the NiagaraComputeMaxThreadGroupSize(%d)"), ShaderPlatform, ThreadGroupSize, NiagaraComputeMaxThreadGroupSize);
		const uint32 TotalThreadGroups = FMath::DivideAndRoundUp(TotalNumInstances, ThreadGroupSize);

		const uint32 ThreadGroupCountZ = 1;
		const uint32 ThreadGroupCountY = FMath::DivideAndRoundUp(TotalThreadGroups, NiagaraMaxThreadGroupCountPerDimension);
		const uint32 ThreadGroupCountX = FMath::DivideAndRoundUp(TotalThreadGroups, ThreadGroupCountY);

		SetShaderValue(RHICmdList, ComputeShader, Shader->DispatchThreadIdToLinearParam, ThreadGroupCountY > 1 ? ThreadGroupCountX * ThreadGroupSize : 0);

		SetConstantBuffers(RHICmdList, Shader, Tick, Instance);

		DispatchComputeShader(RHICmdList, Shader.GetShader(), ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);

		INC_DWORD_STAT(STAT_NiagaraGPUDispatches);
	}

	// Unset UAV parameters.
	// 
	UnsetDataInterfaceParameters(DataInterfaceProxies, Shader, RHICmdList, Instance, Tick, SimulationStageIndex);
	FNiagaraDataBuffer::UnsetShaderParams(RHICmdList, Shader.GetShader());
	Shader->InstanceCountsParam.UnsetUAV(RHICmdList, ComputeShader);

	ResetEmptyUAVPools(RHICmdList);


	// Optionally submit commands to the GPU
	// This can be used to avoid accidental TDR detection in the editor especially when issuing multiple ticks in the same frame
	if (GNiagaraGpuSubmitCommandHint > 0)
	{
		++TotalDispatchesThisFrame;
		if ((TotalDispatchesThisFrame % GNiagaraGpuSubmitCommandHint) == 0)
		{
			RHICmdList.SubmitCommandsHint();
		}
	}
}

FGPUSortManager* NiagaraEmitterInstanceBatcher::GetGPUSortManager() const
{
	return GPUSortManager;
}

#if WITH_EDITORONLY_DATA
void NiagaraEmitterInstanceBatcher::AddDebugReadback(FNiagaraSystemInstanceID InstanceID, TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo, FNiagaraComputeExecutionContext* Context)
{
	FDebugReadbackInfo& ReadbackInfo = GpuDebugReadbackInfos.AddDefaulted_GetRef();
	ReadbackInfo.InstanceID = InstanceID;
	ReadbackInfo.DebugInfo = DebugInfo;
	ReadbackInfo.Context = Context;
}
#endif


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

	RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
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
		// Dispatches which use dummy UAVs are allowed to overlap, since we don't care about the contents of these buffers.
		// We never need to calll EndUAVOverlap() on these.
		RHICmdList.BeginUAVOverlap(NewUAV.UAV);
	}

	FRHIUnorderedAccessView* UAV = Pool.UAVs[Pool.NextFreeIndex].UAV;
	++Pool.NextFreeIndex;
	return UAV;
}

void NiagaraEmitterInstanceBatcher::ResetEmptyUAVPool(TMap<EPixelFormat, DummyUAVPool>& UAVMap)
{
	for (TPair<EPixelFormat, DummyUAVPool>& Entry : UAVMap)
	{
		Entry.Value.NextFreeIndex = 0;
	}
}

void NiagaraEmitterInstanceBatcher::ResetEmptyUAVPools(FRHICommandList& RHICmdList)
{
	ResetEmptyUAVPool(DummyBufferPool);
	ResetEmptyUAVPool(DummyTexturePool);
}

bool NiagaraEmitterInstanceBatcher::ShouldDebugDraw_RenderThread() const
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	if (FNiagaraGpuComputeDebug* GpuComputeDebug = GpuComputeDebugPtr.Get())
	{
		return GpuComputeDebug->ShouldDrawDebug();
	}
#endif
	return false;
}

void NiagaraEmitterInstanceBatcher::DrawDebug_RenderThread(class FRDGBuilder& GraphBuilder, const class FViewInfo& View, const struct FScreenPassRenderTarget& Output)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	if (FNiagaraGpuComputeDebug* GpuComputeDebug = GpuComputeDebugPtr.Get())
	{
		GpuComputeDebug->DrawDebug(GraphBuilder, View, Output);
	}
#endif
}

void NiagaraEmitterInstanceBatcher::DrawSceneDebug_RenderThread(class FRDGBuilder& GraphBuilder, const class FViewInfo& View, FRDGTextureRef SceneColor, FRDGTextureRef SceneDepth)
{
#if NIAGARA_COMPUTEDEBUG_ENABLED
	if (FNiagaraGpuComputeDebug* GpuComputeDebug = GpuComputeDebugPtr.Get())
	{
		GpuComputeDebug->DrawSceneDebug(GraphBuilder, View, SceneColor, SceneDepth);
	}
#endif
}
