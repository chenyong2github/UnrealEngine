// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraSystemGpuComputeProxy.h"
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
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("# DummyUAVs"), STAT_NiagaraDummyUAVPool, STATGROUP_Niagara);

DECLARE_GPU_STAT_NAMED(NiagaraGPU, TEXT("Niagara"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSimulation, TEXT("Niagara GPU Simulation"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUClearIDTables, TEXT("NiagaraGPU Clear ID Tables"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUComputeFreeIDs, TEXT("Niagara GPU Compute All Free IDs"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUComputeFreeIDsEmitter, TEXT("Niagara GPU Compute Emitter Free IDs"));
DECLARE_GPU_STAT_NAMED(NiagaraGPUSorting, TEXT("Niagara GPU sorting"));

uint32 FNiagaraComputeExecutionContext::TickCounter = 0;

#if WITH_MGPU
const FName NiagaraEmitterInstanceBatcher::TemporalEffectName("NiagaraEmitterInstanceBatcher");
#endif // WITH_MGPU

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

int32 GNiagaraBatcherFreeBufferEarly = 1;
static FAutoConsoleVariableRef CVarNiagaraBatcherFreeBufferEarly(
	TEXT("fx.NiagaraBatcher.FreeBufferEarly"),
	GNiagaraBatcherFreeBufferEarly,
	TEXT("Will take the path to release GPU buffers when possible.\n")
	TEXT("This will reduce memory pressure but can result in more allocations if you buffers ping pong from zero particles to many."),
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

	static int32 GAddDispatchGroupDrawEvent = 0;
	static FAutoConsoleVariableRef CVarAddDispatchGroupDrawEvent(
		TEXT("fx.Niagara.Batcher.AddDispatchGroupDrawEvent"),
		GAddDispatchGroupDrawEvent,
		TEXT("Add a draw event marker around each dispatch group."),
		ECVF_Default
	);

	#if !WITH_EDITOR
		constexpr int32 GDebugLogging = 0;
	#else
		static int32 GDebugLogging = 0;
		static FAutoConsoleVariableRef CVarNiagaraDebugLogging(
			TEXT("fx.Niagara.Batcher.DebugLogging"),
			GDebugLogging,
			TEXT("Enables a lot of spew to the log to debug the batcher."),
			ECVF_Default
		);
	#endif

	template<typename TTransitionArrayType>
	static void AddDataBufferTransitions(TTransitionArrayType& BeforeTransitionArray, TTransitionArrayType& AfterTransitionArray, FNiagaraDataBuffer* DestinationData, ERHIAccess BeforeState = ERHIAccess::SRVMask, ERHIAccess AfterState = ERHIAccess::UAVCompute)
	{
		if (FRHIUnorderedAccessView* FloatUAV = DestinationData->GetGPUBufferFloat().UAV )
		{
			BeforeTransitionArray.Emplace(FloatUAV, BeforeState, AfterState);
			AfterTransitionArray.Emplace(FloatUAV, AfterState, BeforeState);
		}
		if (FRHIUnorderedAccessView* HalfUAV = DestinationData->GetGPUBufferHalf().UAV)
		{
			BeforeTransitionArray.Emplace(HalfUAV, BeforeState, AfterState);
			AfterTransitionArray.Emplace(HalfUAV, AfterState, BeforeState);
		}
		if (FRHIUnorderedAccessView* IntUAV = DestinationData->GetGPUBufferInt().UAV)
		{
			BeforeTransitionArray.Emplace(IntUAV, BeforeState, AfterState);
			AfterTransitionArray.Emplace(IntUAV, AfterState, BeforeState);
		}
	}

	static bool ShouldRunStage(FNiagaraComputeExecutionContext* ComputeContext, FNiagaraDataInterfaceProxyRW* IterationInterface, uint32 StageIndex, bool bResetData)
	{
		if (!IterationInterface || ComputeContext->SpawnStages.Num() == 0)
		{
			return true;
		}

		const bool bIsSpawnStage = ComputeContext->SpawnStages.Contains(StageIndex);
		return bResetData == bIsSpawnStage;
	}
}

FNiagaraUAVPoolAccessScope::FNiagaraUAVPoolAccessScope(NiagaraEmitterInstanceBatcher& InBatcher)
	: Batcher(InBatcher)
{
	++Batcher.DummyUAVAccessCounter;
}

FNiagaraUAVPoolAccessScope::~FNiagaraUAVPoolAccessScope()
{
	--Batcher.DummyUAVAccessCounter;
	if (Batcher.DummyUAVAccessCounter == 0)
	{
		Batcher.ResetEmptyUAVPools();
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

		if (FNiagaraUtilities::AllowComputeShaders(GetShaderPlatform()))
		{
			// Because of culled indirect draw args, we have to update the draw indirect buffer after the sort key generation
			GPUSortManager->PostPreRenderEvent.AddLambda(
				[this](FRHICommandListImmediate& RHICmdList)
				{
					GPUInstanceCounterManager.UpdateDrawIndirectBuffers(*this, RHICmdList, FeatureLevel);
#if WITH_MGPU
					// For PreInitViews we actually need to broadcast here and not in ExecuteAll since
					// this is the last place the instance count buffer is written to.
					if (StageToBroadcastTemporalEffect == ENiagaraGpuComputeTickStage::PreInitViews)
					{
						BroadcastTemporalEffect(RHICmdList);
					}
#endif // WITH_MGPU
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

void NiagaraEmitterInstanceBatcher::AddGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy)
{
	check(ComputeProxy->BatcherIndex == INDEX_NONE);

	const ENiagaraGpuComputeTickStage::Type TickStage = ComputeProxy->GetComputeTickStage();
	ComputeProxy->BatcherIndex = ProxiesPerStage[TickStage].Num();
	ProxiesPerStage[TickStage].Add(ComputeProxy);

	NumProxiesThatRequireDistanceFieldData	+= ComputeProxy->RequiresDistanceFieldData() ? 1 : 0;
	NumProxiesThatRequireDepthBuffer		+= ComputeProxy->RequiresDepthBuffer() ? 1 : 0;
	NumProxiesThatRequireEarlyViewData		+= ComputeProxy->RequiresEarlyViewData() ? 1 : 0;
}

void NiagaraEmitterInstanceBatcher::RemoveGpuComputeProxy(FNiagaraSystemGpuComputeProxy* ComputeProxy)
{
	check(ComputeProxy->BatcherIndex != INDEX_NONE);

	const int32 TickStage = int32(ComputeProxy->GetComputeTickStage());
	const int32 ProxyIndex = ComputeProxy->BatcherIndex;
	check(ProxiesPerStage[TickStage][ProxyIndex] == ComputeProxy);

	ProxiesPerStage[TickStage].RemoveAtSwap(ProxyIndex);
	if (ProxiesPerStage[TickStage].IsValidIndex(ProxyIndex))
	{
		ProxiesPerStage[TickStage][ProxyIndex]->BatcherIndex = ProxyIndex;
	}
	ComputeProxy->BatcherIndex = INDEX_NONE;

	NumProxiesThatRequireDistanceFieldData	-= ComputeProxy->RequiresDistanceFieldData() ? 1 : 0;
	NumProxiesThatRequireDepthBuffer		-= ComputeProxy->RequiresDepthBuffer() ? 1 : 0;
	NumProxiesThatRequireEarlyViewData		-= ComputeProxy->RequiresEarlyViewData() ? 1 : 0;

#if NIAGARA_COMPUTEDEBUG_ENABLED
	if (FNiagaraGpuComputeDebug* GpuComputeDebug = GpuComputeDebugPtr.Get())
	{
		GpuComputeDebug->OnSystemDeallocated(ComputeProxy->GetSystemInstanceID());
	}
#endif
#if !UE_BUILD_SHIPPING
	GpuDebugReadbackInfos.RemoveAll(
		[&](const FDebugReadbackInfo& Info)
		{
			// In the unlikely event we have one in the queue make sure it's marked as complete with no data in it
			if ( Info.InstanceID == ComputeProxy->GetSystemInstanceID())
			{
				Info.DebugInfo->Frame.CopyFromGPUReadback(nullptr, nullptr, nullptr, 0, 0, 0, 0, 0);
				Info.DebugInfo->bWritten = true;
			}
			return Info.InstanceID == ComputeProxy->GetSystemInstanceID();
		}
	);
#endif
}

void NiagaraEmitterInstanceBatcher::BuildConstantBuffers(FNiagaraGPUSystemTick& Tick) const
{
	if (!Tick.InstanceCount)
	{
		return;
	}

	TArrayView<FNiagaraComputeInstanceData> InstanceData = Tick.GetInstances();

	// first go through and figure out if we need to support interpolated spawning
	bool HasInterpolationParameters = false;
	bool HasMultipleStages = false;
	for (const FNiagaraComputeInstanceData& Instance : InstanceData)
	{
		HasInterpolationParameters = HasInterpolationParameters || Instance.Context->HasInterpolationParameters;
		HasMultipleStages = HasMultipleStages || Instance.bUsesOldShaderStages || Instance.bUsesSimStages;
	}

	int32 BoundParameterCounts[FNiagaraGPUSystemTick::UBT_NumTypes][2];
	FMemory::Memzero(BoundParameterCounts);

	for (const FNiagaraComputeInstanceData& Instance : InstanceData)
	{
		for (int32 InterpIt = 0; InterpIt < (HasInterpolationParameters ? 2 : 1); ++InterpIt)
		{
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Global][InterpIt] += Instance.Context->GPUScript_RT->IsGlobalConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_System][InterpIt] += Instance.Context->GPUScript_RT->IsSystemConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Owner][InterpIt] += Instance.Context->GPUScript_RT->IsOwnerConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_Emitter][InterpIt] += Instance.Context->GPUScript_RT->IsEmitterConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
			BoundParameterCounts[FNiagaraGPUSystemTick::UBT_External][InterpIt] += Instance.Context->GPUScript_RT->IsExternalConstantBufferUsed_RenderThread(InterpIt) ? 1 : 0;
		}
	}

	const int32 InterpScale = HasInterpolationParameters ? 2 : 1;
	const int32 BufferCount = InterpScale * (FNiagaraGPUSystemTick::UBT_NumSystemTypes + FNiagaraGPUSystemTick::UBT_NumInstanceTypes * Tick.InstanceCount);

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
			for (const FNiagaraComputeInstanceData& Instance : InstanceData)
			{
				FUniformBufferRHIRef BufferRef;

				if (BoundParameterCounts[InstanceTypeIt][InterpIt])
				{
					FRHIUniformBufferLayout& Layout = InstanceTypeIt == FNiagaraGPUSystemTick::UBT_Emitter ? EmitterCBufferLayout->UBLayout : Instance.Context->ExternalCBufferLayout->UBLayout;
					if (Layout.Resources.Num() > 0 || Layout.ConstantBufferSize > 0)
					{
						BufferRef = RHICreateUniformBuffer(
							Tick.GetUniformBufferSource((FNiagaraGPUSystemTick::EUniformBufferType) InstanceTypeIt, &Instance, !InterpIt),
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

void NiagaraEmitterInstanceBatcher::Tick(float DeltaTime)
{
	check(IsInGameThread());
	ENQUEUE_RENDER_COMMAND(NiagaraPumpBatcher)(
		[RT_NiagaraBatcher=this](FRHICommandListImmediate& RHICmdList)
		{
			RT_NiagaraBatcher->ProcessPendingTicksFlush(RHICmdList, false);
			RT_NiagaraBatcher->GetGPUInstanceCounterManager().FlushIndirectArgsPool();
		}
	);
}

void NiagaraEmitterInstanceBatcher::ProcessPendingTicksFlush(FRHICommandListImmediate& RHICmdList, bool bForceFlush)
{
	// No ticks are pending
	bool bHasTicks = false;
	for ( int iTickStage=0; iTickStage < ENiagaraGpuComputeTickStage::Max; ++iTickStage)
	{
		if ( ProxiesPerStage[iTickStage].Num() > 0 )
		{
			bHasTicks = true;
			break;
		}
	}

	if ( !bHasTicks )
	{
		return;
	}

	// We have pending ticks increment our counter, once we cross the threshold we will perform the appropriate operation
	++FramesBeforeTickFlush;
	if (!bForceFlush && (FramesBeforeTickFlush < uint32(NiagaraEmitterInstanceBatcherLocal::GTickFlushMaxQueuedFrames)) )
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

			const FIntRect DummViewRect(0, 0, 128, 128);
			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.ViewFamily = &ViewFamily;
			ViewInitOptions.SetViewRectangle(DummViewRect);
			ViewInitOptions.ViewOrigin = FVector::ZeroVector;
			ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
			ViewInitOptions.ProjectionMatrix = FMatrix::Identity;

			FViewInfo View(ViewInitOptions);
			View.ViewRect = View.UnscaledViewRect;
			View.CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

			FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(FRHICommandListExecutor::GetImmediateCommandList());

			// This function might run before the scene context is set up, in which case we just want a dummy uniform buffer to allow us to run the ticks without crashing.
			ESceneTextureSetupMode SceneUniformBufSetupMode = SceneContext.IsShadingPathValid() ? ESceneTextureSetupMode::All : ESceneTextureSetupMode::None;

			FBox UnusedVolumeBounds[TVC_MAX];
			View.SetupUniformBufferParameters(SceneContext, UnusedVolumeBounds, TVC_MAX, *View.CachedViewUniformShaderParameters);

			View.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*View.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);

			// Execute all ticks that we can support without invalid simulations
			PreInitViews(RHICmdList, true);
			GPUInstanceCounterManager.UpdateDrawIndirectBuffers(*this, RHICmdList, FeatureLevel);
			PostInitViews(RHICmdList, View.ViewUniformBuffer, true);
			PostRenderOpaque(RHICmdList, View.ViewUniformBuffer, &FSceneTextureUniformParameters::StaticStructMetadata, CreateSceneTextureUniformBuffer(RHICmdList, FeatureLevel, SceneUniformBufSetupMode), true);
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
	check(IsInRenderingThread());

	for (int iTickStage = 0; iTickStage < ENiagaraGpuComputeTickStage::Max; ++iTickStage)
	{
		for (FNiagaraSystemGpuComputeProxy* ComputeProxy : ProxiesPerStage[iTickStage])
		{
			ComputeProxy->ReleaseTicks(GPUInstanceCounterManager);
		}
	}

	for (FNiagaraGpuDispatchList& DispatchList : DispatchListPerStage)
	{
		DispatchList.DispatchGroups.Empty();
		if (DispatchList.CountsToRelease.Num() > 0)
		{
			GPUInstanceCounterManager.FreeEntryArray(DispatchList.CountsToRelease);
			DispatchList.CountsToRelease.Empty();
		}
	}
}

void NiagaraEmitterInstanceBatcher::ResetDataInterfaces(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData) const
{
	// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
	const FNiagaraShaderRef& ComputeShader = InstanceData.Context->GPUScript_RT->GetShader(0);

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : InstanceData.DataInterfaceProxies)
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

FNiagaraDataInterfaceProxyRW* NiagaraEmitterInstanceBatcher::FindIterationInterface(FNiagaraComputeInstanceData* Instance, const uint32 SimulationStageIndex) const
{
	// Determine if the iteration is outputting to a custom data size
	return Instance->FindIterationInterface(SimulationStageIndex);
}

void NiagaraEmitterInstanceBatcher::PreStageInterface(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraSimStageData& SimStageData) const
{
	// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
	const FNiagaraShaderRef& ComputeShader = InstanceData.Context->GPUScript_RT->GetShader(0);

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : InstanceData.DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			const FNiagaraDataInterfaceStageArgs TmpContext(Interface, Tick.SystemInstanceID, this, &InstanceData, &SimStageData, InstanceData.IsOutputStage(Interface, SimStageData.StageIndex), InstanceData.IsIterationStage(Interface, SimStageData.StageIndex));
			Interface->PreStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::PostStageInterface(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraSimStageData& SimStageData) const
{
	// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
	const FNiagaraShaderRef& ComputeShader = InstanceData.Context->GPUScript_RT->GetShader(0);

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : InstanceData.DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			const FNiagaraDataInterfaceStageArgs TmpContext(Interface, Tick.SystemInstanceID, this, &InstanceData, &SimStageData, InstanceData.IsOutputStage(Interface, SimStageData.StageIndex), InstanceData.IsIterationStage(Interface, SimStageData.StageIndex));
			Interface->PostStage(RHICmdList, TmpContext);
		}
		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::PostSimulateInterface(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData) const
{
	// Note: All stages will contain the same bindings so if they are valid for one they are valid for all, this could change in the future
	const FNiagaraShaderRef& ComputeShader = InstanceData.Context->GPUScript_RT->GetShader(0);

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : InstanceData.DataInterfaceProxies)
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


void NiagaraEmitterInstanceBatcher::UpdateFreeIDsListSizesBuffer(FRHICommandList& RHICmdList, uint32 NumInstances)
{
	ERHIAccess BeforeState = ERHIAccess::UAVCompute;
	if (NumInstances > NumAllocatedFreeIDListSizes)
	{
		constexpr uint32 ALLOC_CHUNK_SIZE = 128;
		NumAllocatedFreeIDListSizes = Align(NumInstances, ALLOC_CHUNK_SIZE);
		if (FreeIDListSizesBuffer.Buffer)
		{
			FreeIDListSizesBuffer.Release();
		}
		FreeIDListSizesBuffer.Initialize(sizeof(uint32), NumAllocatedFreeIDListSizes, EPixelFormat::PF_R32_SINT, BUF_Static, TEXT("NiagaraFreeIDListSizes"));

		BeforeState = ERHIAccess::Unknown;
	}

	{
		SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUComputeClearFreeIDListSizes);
		RHICmdList.Transition(FRHITransitionInfo(FreeIDListSizesBuffer.UAV, BeforeState, ERHIAccess::UAVCompute));
		NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, FreeIDListSizesBuffer, 0);
	}
}

void NiagaraEmitterInstanceBatcher::UpdateFreeIDBuffers(FRHICommandList& RHICmdList, TConstArrayView<FNiagaraComputeExecutionContext*> Instances)
{
	if (Instances.Num() == 0)
	{
		return;
	}

	SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUComputeFreeIDs);
	SCOPED_GPU_STAT(RHICmdList, NiagaraGPUComputeFreeIDs);

	TArray<FRHITransitionInfo, TMemStackAllocator<>> TransitionsBefore;
	TransitionsBefore.Emplace(FreeIDListSizesBuffer.UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute);
	RHICmdList.Transition(TransitionsBefore);

	check((uint32)Instances.Num() <= NumAllocatedFreeIDListSizes);

	RHICmdList.BeginUAVOverlap(FreeIDListSizesBuffer.UAV);
	for (uint32 iInstance = 0; iInstance < (uint32)Instances.Num(); ++iInstance)
	{
		FNiagaraComputeExecutionContext* ComputeContext = Instances[iInstance];
		FNiagaraDataSet* MainDataSet = ComputeContext->MainDataSet;
		FNiagaraDataBuffer* CurrentData = ComputeContext->MainDataSet->GetCurrentData();

		SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUComputeFreeIDsEmitter, TEXT("Update Free ID Buffer - %s"), ComputeContext->GetDebugSimName());
		NiagaraComputeGPUFreeIDs(RHICmdList, FeatureLevel, MainDataSet->GetGPUNumAllocatedIDs(), CurrentData->GetGPUIDToIndexTable().SRV, MainDataSet->GetGPUFreeIDs(), FreeIDListSizesBuffer, iInstance);
	}
	RHICmdList.EndUAVOverlap(FreeIDListSizesBuffer.UAV);
}

void NiagaraEmitterInstanceBatcher::DumpDebugFrame(FRHICommandListImmediate& RHICmdList)
{
	// Anything doing?
	bool bHasAnyWork = false;
	for (int iTickStage = 0; iTickStage < ENiagaraGpuComputeTickStage::Max; ++iTickStage)
	{
		bHasAnyWork |= DispatchListPerStage[iTickStage].HasWork();
	}
	if ( !bHasAnyWork )
	{
		return;
	}

	// Dump Frame
	UE_LOG(LogNiagara, Warning, TEXT("====== BatcherFrame(%d)"), GFrameNumberRenderThread);

	for (int iTickStage = 0; iTickStage < ENiagaraGpuComputeTickStage::Max; ++iTickStage)
	{
		if (!DispatchListPerStage[iTickStage].HasWork())
		{
			continue;
		}

		FNiagaraGpuDispatchList& DispatchList = DispatchListPerStage[iTickStage];
		UE_LOG(LogNiagara, Warning, TEXT("==== TickStage(%d) TotalGroups(%d)"), iTickStage, DispatchList.DispatchGroups.Num());

		for ( int iDispatchGroup=0; iDispatchGroup < DispatchList.DispatchGroups.Num(); ++iDispatchGroup )
		{
			const FNiagaraGpuDispatchGroup& DispatchGroup = DispatchListPerStage[iTickStage].DispatchGroups[iDispatchGroup];

			if (DispatchGroup.TicksWithPerInstanceData.Num() > 0)
			{
				UE_LOG(LogNiagara, Warning, TEXT("====== TicksWithPerInstanceData(%s)"), DispatchGroup.TicksWithPerInstanceData.Num());
				for (FNiagaraGPUSystemTick* Tick : DispatchGroup.TicksWithPerInstanceData)
				{
					for (const auto& Pair : Tick->DIInstanceData->InterfaceProxiesToOffsets)
					{
						FNiagaraDataInterfaceProxy* Proxy = Pair.Key;
						UE_LOG(LogNiagara, Warning, TEXT("Proxy(%s)"), *Proxy->SourceDIName.ToString());
					}
				}
			}

			UE_LOG(LogNiagara, Warning, TEXT("====== DispatchGroup(%d)"), iDispatchGroup);
			for ( const FNiagaraGpuDispatchInstance& DispatchInstance : DispatchGroup.DispatchInstances )
			{
				const FNiagaraSimStageData& SimStageData = DispatchInstance.SimStageData;
				const FNiagaraComputeInstanceData& InstanceData = DispatchInstance.InstanceData;

				TStringBuilder<512> Builder;
				Builder.Appendf(TEXT("Proxy(%p) "), DispatchInstance.Tick.SystemGpuComputeProxy);
				Builder.Appendf(TEXT("ComputeContext(%p) "), InstanceData.Context);
				Builder.Appendf(TEXT("Emitter(%s) "), InstanceData.Context->GetDebugSimName());
				if (SimStageData.StageMetaData)
				{
					Builder.Appendf(TEXT("Stage(%d | %s) "), SimStageData.StageIndex, *SimStageData.StageMetaData->SimulationStageName.ToString());
				}
				else
				{
					Builder.Appendf(TEXT("Stage(%d) "), SimStageData.StageIndex);
				}

				if (InstanceData.bResetData)
				{
					Builder.Append(TEXT("ResetData "));
				}

				if (InstanceData.Context->MainDataSet->RequiresPersistentIDs())
				{
					Builder.Append(TEXT("HasPersistentIDs "));
				}

				if ( DispatchInstance.SimStageData.bFirstStage )
				{
					Builder.Append(TEXT("FirstStage "));
				}

				if ( DispatchInstance.SimStageData.bLastStage )
				{
					Builder.Append(TEXT("LastStage "));
				}

				if (DispatchInstance.SimStageData.bSetDataToRender)
				{
					Builder.Append(TEXT("SetDataToRender "));
				}
					

				if (InstanceData.Context->EmitterInstanceReadback.GPUCountOffset != INDEX_NONE)
				{
					if (InstanceData.Context->EmitterInstanceReadback.GPUCountOffset == SimStageData.SourceCountOffset)
					{
						Builder.Appendf(TEXT("ReadbackSource(%d) "), InstanceData.Context->EmitterInstanceReadback.CPUCount);
					}
				}
				Builder.Appendf(TEXT("Source(%p 0x%08x %d) "), SimStageData.Source, SimStageData.SourceCountOffset, SimStageData.SourceNumInstances);
				Builder.Appendf(TEXT("Destination(%p 0x%08x %d) "), SimStageData.Destination, SimStageData.DestinationCountOffset, SimStageData.DestinationNumInstances);
				Builder.Appendf(TEXT("Iteration(%s) "), SimStageData.AlternateIterationSource ? *SimStageData.AlternateIterationSource->SourceDIName.ToString() : TEXT("Particles"));
				UE_LOG(LogNiagara, Warning, TEXT("%s"), Builder.ToString());
			}

			if (DispatchGroup.FreeIDUpdates.Num() > 0)
			{
				UE_LOG(LogNiagara, Warning, TEXT("====== FreeIDUpdates"));
				for (FNiagaraComputeExecutionContext* ComputeContext : DispatchGroup.FreeIDUpdates)
				{
					UE_LOG(LogNiagara, Warning, TEXT("ComputeContext(%p) Emitter(%s)"), ComputeContext, ComputeContext->GetDebugSimName());
				}
			}
		}
		if (DispatchList.CountsToRelease.Num() > 0)
		{
			UE_LOG(LogNiagara, Warning, TEXT("====== CountsToRelease"));

			const int NumPerLine = 16;

			TStringBuilder<512> StringBuilder;
			for ( int32 i=0; i < DispatchList.CountsToRelease.Num(); ++i )
			{
				const bool bFirst = (i % NumPerLine) == 0;
				const bool bLast = ((i % NumPerLine) == NumPerLine - 1) || (i == DispatchList.CountsToRelease.Num() -1);

				if ( !bFirst )
				{
					StringBuilder.Append(TEXT(", "));
				}
				StringBuilder.Appendf(TEXT("0x%08x"), DispatchList.CountsToRelease[i]);

				if ( bLast )
				{
					UE_LOG(LogNiagara, Warning, TEXT("%s"), StringBuilder.ToString());
					StringBuilder.Reset();
				}
			}
		}
	}
}

void NiagaraEmitterInstanceBatcher::UpdateInstanceCountManager(FRHICommandListImmediate& RHICmdList)
{
	// Resize dispatch buffer count
	//-OPT: No need to iterate over all the ticks, we can store this as ticks are queued
	{
		int32 TotalDispatchCount = 0;
		for (int iTickStage = 0; iTickStage < ENiagaraGpuComputeTickStage::Max; ++iTickStage)
		{
			for (FNiagaraSystemGpuComputeProxy* ComputeProxy : ProxiesPerStage[iTickStage])
			{
				for (FNiagaraGPUSystemTick& Tick : ComputeProxy->PendingTicks)
				{
					TotalDispatchCount += (int32)Tick.TotalDispatches;

					for ( FNiagaraComputeInstanceData& InstanceData : Tick.GetInstances() )
					{
						if (InstanceData.bResetData)
						{
							GPUInstanceCounterManager.FreeEntry(InstanceData.Context->EmitterInstanceReadback.GPUCountOffset);
						}
					}
				}
			}
		}
		GPUInstanceCounterManager.ResizeBuffers(RHICmdList, FeatureLevel, TotalDispatchCount);
	}

	// Consume any pending readbacks that are ready
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUReadback_RT);
		if ( const uint32* Counts = GPUInstanceCounterManager.GetGPUReadback() )
		{
			if (NiagaraEmitterInstanceBatcherLocal::GDebugLogging)
			{
				UE_LOG(LogNiagara, Warning, TEXT("====== BatcherFrame(%d) Readback Complete"), GFrameNumberRenderThread);
			}

			for (int iTickStage=0; iTickStage < ENiagaraGpuComputeTickStage::Max; ++iTickStage)
			{
				for (FNiagaraSystemGpuComputeProxy* ComputeProxy : ProxiesPerStage[iTickStage])
				{
					for ( FNiagaraComputeExecutionContext* ComputeContext : ComputeProxy->ComputeContexts )
					{
						if (ComputeContext->EmitterInstanceReadback.GPUCountOffset == INDEX_NONE )
						{
							continue;
						}

						const uint32 DeadInstanceCount = ComputeContext->EmitterInstanceReadback.CPUCount - Counts[ComputeContext->EmitterInstanceReadback.GPUCountOffset];
						if ( DeadInstanceCount <= ComputeContext->CurrentNumInstances_RT )
						{
							ComputeContext->CurrentNumInstances_RT -= DeadInstanceCount;
						}
						if (NiagaraEmitterInstanceBatcherLocal::GDebugLogging)
						{
							UE_LOG(LogNiagara, Warning, TEXT("ComputeContext(%p) Emitter(%s) DeadInstances(%d) CountReleased(0x%08x)"), ComputeContext, ComputeContext->GetDebugSimName(), DeadInstanceCount, ComputeContext->EmitterInstanceReadback.GPUCountOffset);
						}

						// Release the counter for the readback
						GPUInstanceCounterManager.FreeEntry(ComputeContext->EmitterInstanceReadback.GPUCountOffset);
					}
				}
			}

			// Release the readback buffer
			GPUInstanceCounterManager.ReleaseGPUReadback();
		}
	}
}

void NiagaraEmitterInstanceBatcher::PrepareTicksForProxy(FRHICommandListImmediate& RHICmdList, FNiagaraSystemGpuComputeProxy* ComputeProxy, FNiagaraGpuDispatchList& GpuDispatchList)
{
	for (FNiagaraComputeExecutionContext* ComputeContext : ComputeProxy->ComputeContexts)
	{
		ComputeContext->CurrentMaxInstances_RT = 0;
		ComputeContext->CurrentMaxAllocateInstances_RT = 0;
		ComputeContext->BufferSwapsThisFrame_RT = 0;
	}

	if (ComputeProxy->PendingTicks.Num() == 0)
	{
		return;
	}

	const bool bEnqueueCountReadback = !GPUInstanceCounterManager.HasPendingGPUReadback();

	// Set final tick flag
	ComputeProxy->PendingTicks.Last().bIsFinalTick = true;

	// Process ticks
	int32 iTickStartDispatchGroup = 0;

	for (FNiagaraGPUSystemTick& Tick : ComputeProxy->PendingTicks)
	{
		int32 iInstanceStartDispatchGroup = iTickStartDispatchGroup;
		int32 iInstanceCurrDispatchGroup = iTickStartDispatchGroup;
		bool bHasFreeIDUpdates = false;

		// Track that we need to consume per instance data before executing the ticks
		//if (Tick.DIInstanceData)
		//{
		//	GpuDispatchList.PreAllocateGroups(iTickStartDispatchGroup + 1);
		//	GpuDispatchList.DispatchGroups[iTickStartDispatchGroup].TicksWithPerInstanceData.Add(&Tick);
		//}

		// Iterate over all instances preparing our number of instances
		for (FNiagaraComputeInstanceData& InstanceData : Tick.GetInstances())
		{
			FNiagaraComputeExecutionContext* ComputeContext = InstanceData.Context;

			// Instance requires a reset?
			if (InstanceData.bResetData)
			{
				ComputeContext->CurrentNumInstances_RT = 0;
				if (ComputeContext->CountOffset_RT != INDEX_NONE)
				{
					GpuDispatchList.CountsToRelease.Add(ComputeContext->CountOffset_RT);
					ComputeContext->CountOffset_RT = INDEX_NONE;
				}
			}

			// If shader is not ready don't do anything
			if (!ComputeContext->GPUScript_RT->IsShaderMapComplete_RenderThread())
			{
				continue;
			}

#if WITH_EDITOR
			//-TODO: Temporary change to avoid crashing on incorrect FeatureLevel
			if (ComputeContext->GPUScript_RT->GetFeatureLevel() != FeatureLevel)
			{
				continue;
			}
#endif

			// Determine this instances start dispatch group, in the case of emitter dependencies (i.e. particle reads) we need to continue rather than starting again
			iInstanceStartDispatchGroup = InstanceData.bStartNewOverlapGroup ? iInstanceCurrDispatchGroup : iInstanceStartDispatchGroup;
			iInstanceCurrDispatchGroup = iInstanceStartDispatchGroup;

			GpuDispatchList.PreAllocateGroups(iInstanceCurrDispatchGroup + ComputeContext->MaxUpdateIterations);

			// Calculate instance counts
			const uint32 MaxBufferInstances = ComputeContext->MainDataSet->GetMaxInstanceCount();
			const uint32 PrevNumInstances = ComputeContext->CurrentNumInstances_RT;

			ComputeContext->CurrentNumInstances_RT = PrevNumInstances + InstanceData.SpawnInfo.SpawnRateInstances + InstanceData.SpawnInfo.EventSpawnTotal;
			ComputeContext->CurrentNumInstances_RT = FMath::Min(ComputeContext->CurrentNumInstances_RT, MaxBufferInstances);

			// Calculate new maximum count
			ComputeContext->CurrentMaxInstances_RT = FMath::Max(ComputeContext->CurrentMaxInstances_RT, ComputeContext->CurrentNumInstances_RT);

			if (!GNiagaraBatcherFreeBufferEarly || (ComputeContext->CurrentMaxInstances_RT > 0))
			{
				ComputeContext->CurrentMaxAllocateInstances_RT = FMath::Max3(ComputeContext->CurrentMaxAllocateInstances_RT, ComputeContext->CurrentMaxInstances_RT, InstanceData.SpawnInfo.MaxParticleCount);
			}
			else
			{
				ComputeContext->CurrentMaxAllocateInstances_RT = FMath::Max(ComputeContext->CurrentMaxAllocateInstances_RT, ComputeContext->CurrentMaxInstances_RT);
			}

			bHasFreeIDUpdates |= ComputeContext->MainDataSet->RequiresPersistentIDs();

			// Add particle update / spawn stage
			{
				FNiagaraGpuDispatchGroup& DispatchGroup = GpuDispatchList.DispatchGroups[iInstanceCurrDispatchGroup++];
				FNiagaraGpuDispatchInstance& DispatchInstance = DispatchGroup.DispatchInstances.Emplace_GetRef(Tick, InstanceData);
				FNiagaraSimStageData& SimStageData = DispatchInstance.SimStageData;

				SimStageData.bFirstStage = true;
				SimStageData.StageIndex = 0;
				SimStageData.Source = ComputeContext->bHasTickedThisFrame_RT ? ComputeContext->GetPrevDataBuffer() : ComputeContext->MainDataSet->GetCurrentData();
				SimStageData.SourceCountOffset = ComputeContext->CountOffset_RT;
				SimStageData.SourceNumInstances = PrevNumInstances;

				// Validation as a non zero instance count must have a valid count offset
				check(SimStageData.SourceCountOffset != INDEX_NONE || SimStageData.SourceNumInstances == 0);

				SimStageData.Destination = ComputeContext->GetNextDataBuffer();
				SimStageData.DestinationCountOffset = GPUInstanceCounterManager.AcquireEntry();
				SimStageData.DestinationNumInstances = ComputeContext->CurrentNumInstances_RT;

				ComputeContext->AdvanceDataBuffer();
				ComputeContext->CountOffset_RT = SimStageData.DestinationCountOffset;

				// If we are the last tick then we may want to enqueue for a readback
				// Note: Do not pull count from SimStageData as a reset tick will be INDEX_NONE
				if (SimStageData.SourceCountOffset != INDEX_NONE)
				{
					if (bEnqueueCountReadback && Tick.bIsFinalTick && (ComputeContext->EmitterInstanceReadback.GPUCountOffset == INDEX_NONE))
					{
						bRequiresReadback = true;
						ComputeContext->EmitterInstanceReadback.CPUCount = SimStageData.SourceNumInstances;
						ComputeContext->EmitterInstanceReadback.GPUCountOffset = SimStageData.SourceCountOffset;
					}
					else if (SimStageData.SourceCountOffset != ComputeContext->EmitterInstanceReadback.GPUCountOffset)
					{
						GpuDispatchList.CountsToRelease.Add(SimStageData.SourceCountOffset);
					}
				}
				if (Tick.NumInstancesWithSimStages > 0)
				{
					SimStageData.AlternateIterationSource = InstanceData.FindIterationInterface(0);
				}
			}
			ComputeContext->bHasTickedThisFrame_RT = true;

			//-OPT: Do we need this test?  Can remove in favor of MaxUpdateIterations
			if (Tick.NumInstancesWithSimStages > 0)
			{
				for (uint32 SimStageIndex=1; SimStageIndex < ComputeContext->MaxUpdateIterations; ++SimStageIndex)
				{
					// Determine if the iteration is outputting to a custom data size
					FNiagaraDataInterfaceProxyRW* IterationInterface = InstanceData.FindIterationInterface(SimStageIndex);
					if (!NiagaraEmitterInstanceBatcherLocal::ShouldRunStage(ComputeContext, IterationInterface, SimStageIndex, InstanceData.bResetData))
					{
						continue;
					}

					// Build SimStage data
					FNiagaraGpuDispatchGroup& DispatchGroup = GpuDispatchList.DispatchGroups[iInstanceCurrDispatchGroup++];
					FNiagaraGpuDispatchInstance& DispatchInstance = DispatchGroup.DispatchInstances.Emplace_GetRef(Tick, InstanceData);
					FNiagaraSimStageData& SimStageData = DispatchInstance.SimStageData;
					SimStageData.StageIndex = SimStageIndex;
					SimStageData.StageMetaData = ComputeContext->GetSimStageMetaData(SimStageIndex);
					SimStageData.AlternateIterationSource = IterationInterface;

					//-TODO: REMOVE OLD SIM STAGES
					if (SimStageData.StageMetaData == nullptr)
					{
						SimStageData.Source = ComputeContext->GetPrevDataBuffer();
						SimStageData.SourceCountOffset = ComputeContext->CountOffset_RT;
						SimStageData.SourceNumInstances = ComputeContext->CurrentNumInstances_RT;
						SimStageData.Destination = ComputeContext->GetNextDataBuffer();
						SimStageData.DestinationCountOffset = GPUInstanceCounterManager.AcquireEntry();
						SimStageData.DestinationNumInstances = ComputeContext->CurrentNumInstances_RT;

						if (ensure(SimStageData.SourceCountOffset != INDEX_NONE))
						{
							GpuDispatchList.CountsToRelease.Add(SimStageData.SourceCountOffset);
						}

						ComputeContext->AdvanceDataBuffer();
						ComputeContext->CountOffset_RT = SimStageData.DestinationCountOffset;
					}
					// This stage does not modify particle data, i.e. read only or not related to particles at all
					else if (!SimStageData.StageMetaData->bWritesParticles)
					{
						SimStageData.Source = ComputeContext->GetPrevDataBuffer();
						SimStageData.SourceCountOffset = ComputeContext->CountOffset_RT;
						SimStageData.SourceNumInstances = ComputeContext->CurrentNumInstances_RT;
						SimStageData.Destination = nullptr;
						SimStageData.DestinationCountOffset = ComputeContext->CountOffset_RT;
						SimStageData.DestinationNumInstances = ComputeContext->CurrentNumInstances_RT;
					}
					// This stage writes particles but will not kill any, we can use the buffer as both source and destination
					else if (SimStageData.StageMetaData->bPartialParticleUpdate)
					{
						SimStageData.Source = nullptr;
						SimStageData.SourceCountOffset = ComputeContext->CountOffset_RT;
						SimStageData.SourceNumInstances = ComputeContext->CurrentNumInstances_RT;
						SimStageData.Destination = ComputeContext->GetPrevDataBuffer();
						SimStageData.DestinationCountOffset = ComputeContext->CountOffset_RT;
						SimStageData.DestinationNumInstances = ComputeContext->CurrentNumInstances_RT;
					}
					// This stage may kill particles, we need to allocate a new destination buffer
					else
					{
						SimStageData.Source = ComputeContext->GetPrevDataBuffer();
						SimStageData.SourceCountOffset = ComputeContext->CountOffset_RT;
						SimStageData.SourceNumInstances = ComputeContext->CurrentNumInstances_RT;
						SimStageData.Destination = ComputeContext->GetNextDataBuffer();
						SimStageData.DestinationCountOffset = GPUInstanceCounterManager.AcquireEntry();
						SimStageData.DestinationNumInstances = ComputeContext->CurrentNumInstances_RT;

						if (ensure(SimStageData.SourceCountOffset != INDEX_NONE))
						{
							GpuDispatchList.CountsToRelease.Add(SimStageData.SourceCountOffset);
						}

						ComputeContext->AdvanceDataBuffer();
						ComputeContext->CountOffset_RT = SimStageData.DestinationCountOffset;
					}
				}
			}

			// The final instance in the last group we modified is the final stage for this tick
			FNiagaraGpuDispatchGroup& FinalDispatchGroup = GpuDispatchList.DispatchGroups[iInstanceCurrDispatchGroup - 1];
			FinalDispatchGroup.DispatchInstances.Last().SimStageData.bLastStage = true;
			FinalDispatchGroup.DispatchInstances.Last().SimStageData.bSetDataToRender = Tick.bIsFinalTick;

			// Keep track of where the next set of dispatch should occur
			iTickStartDispatchGroup = FMath::Max(iTickStartDispatchGroup, iInstanceCurrDispatchGroup);
		}

		// Accumulate Free ID updates
		// Note: These must be done at the end of the tick due to the way spawned instances read from the free list
		if (bHasFreeIDUpdates)
		{
			FNiagaraGpuDispatchGroup& FinalDispatchGroup = GpuDispatchList.DispatchGroups[iInstanceCurrDispatchGroup - 1];
			for (FNiagaraComputeInstanceData& InstanceData : Tick.GetInstances())
			{
				FNiagaraComputeExecutionContext* ComputeContext = InstanceData.Context;
				if (!ComputeContext->GPUScript_RT->IsShaderMapComplete_RenderThread())
				{
					continue;
				}

				if ( ComputeContext->MainDataSet->RequiresPersistentIDs() )
				{
					FinalDispatchGroup.FreeIDUpdates.Add(ComputeContext);
				}
			}
		}

		// Build constant buffers for tick
		BuildConstantBuffers(Tick);
	}

	// Now that all ticks have been processed we can adjust our output buffers to the correct size
	// We will also set the translucent data to render, i.e. this frames data.
	for (FNiagaraComputeExecutionContext* ComputeContext : ComputeProxy->ComputeContexts)
	{
		if (!ComputeContext->bHasTickedThisFrame_RT)
		{
			continue;
		}

		// We need to store the current data from the main data set as we will be temporarily stomping it during multi-ticking
		ComputeContext->DataSetOriginalBuffer_RT = ComputeContext->MainDataSet->GetCurrentData();

		//-OPT: We should allocate all GPU free IDs together since they require a transition
		if (ComputeContext->MainDataSet->RequiresPersistentIDs())
		{
			ComputeContext->MainDataSet->AllocateGPUFreeIDs(ComputeContext->CurrentMaxAllocateInstances_RT + 1, RHICmdList, FeatureLevel, ComputeContext->GetDebugSimName());
		}

		// Allocate space for the buffers we need to perform ticking.  In cases of multiple ticks or multiple write stages we need 3 buffers (current rendered and two simulation buffers).
		//-OPT: We can batch the allocation of persistent IDs together so the compute shaders overlap
		const uint32 NumBuffersToResize = FMath::Min<uint32>(ComputeContext->BufferSwapsThisFrame_RT, UE_ARRAY_COUNT(ComputeContext->DataBuffers_RT));
		for (uint32 i=0; i < NumBuffersToResize; ++i)
		{
			ComputeContext->DataBuffers_RT[i]->AllocateGPU(RHICmdList, ComputeContext->CurrentMaxAllocateInstances_RT + 1, FeatureLevel, ComputeContext->GetDebugSimName());
		}

		// Ensure we don't keep multi-tick buffers around longer than they are required by releasing them
		for (uint32 i=NumBuffersToResize; i < UE_ARRAY_COUNT(ComputeContext->DataBuffers_RT); ++i)
		{
			ComputeContext->DataBuffers_RT[i]->ReleaseGPU();
		}

		// RDG will defer the Niagara dispatches until the graph is executed.
		// Therefore we need to setup the DataToRender for MeshProcessors & sorting to use the correct data,
		// that is anything that happens before PostRenderOpaque
		if ((ComputeProxy->GetComputeTickStage() == ENiagaraGpuComputeTickStage::PreInitViews) || (ComputeProxy->GetComputeTickStage() == ENiagaraGpuComputeTickStage::PostInitViews))
		{
			FNiagaraDataBuffer* FinalBuffer = ComputeContext->GetPrevDataBuffer();
			FinalBuffer->GPUInstanceCountBufferOffset = ComputeContext->CountOffset_RT;
			FinalBuffer->SetNumInstances(ComputeContext->CurrentNumInstances_RT);
			ComputeContext->SetDataToRender(FinalBuffer);
#if WITH_MGPU
			AddTemporalEffectBuffers(FinalBuffer);
#endif // WITH_MGPU
		}
		// When low latency translucency is enabled we can setup the final buffer / final count here.
		// This will allow our mesh processor commands to pickup the data for the same frame.
		// This allows simulations that use the depth buffer, for example, to execute with no latency
		else if ( GNiagaraGpuLowLatencyTranslucencyEnabled )
		{
			FNiagaraDataBuffer* FinalBuffer = ComputeContext->GetPrevDataBuffer();
			FinalBuffer->GPUInstanceCountBufferOffset = ComputeContext->CountOffset_RT;
			FinalBuffer->SetNumInstances(ComputeContext->CurrentNumInstances_RT);
			ComputeContext->SetTranslucentDataToRender(FinalBuffer);
		}
	}

#if WITH_MGPU
	if (GNumAlternateFrameRenderingGroups > 1)
	{
		StageToBroadcastTemporalEffect = ENiagaraGpuComputeTickStage::Last;
		while (StageToBroadcastTemporalEffect > ENiagaraGpuComputeTickStage::First && !DispatchListPerStage[static_cast<int32>(StageToBroadcastTemporalEffect)].HasWork())
		{
			StageToBroadcastTemporalEffect = static_cast<ENiagaraGpuComputeTickStage::Type>(static_cast<int32>(StageToBroadcastTemporalEffect) - 1);
		}

		StageToWaitForTemporalEffect = ENiagaraGpuComputeTickStage::First;
		// If we're going to write to the instance count buffer after PreInitViews then
		// that needs to be the wait stage, regardless of whether or not we're ticking
		// anything in that stage.
		if (!GPUInstanceCounterManager.HasEntriesPendingFree())
		{
			while (StageToWaitForTemporalEffect < StageToBroadcastTemporalEffect && !DispatchListPerStage[static_cast<int32>(StageToWaitForTemporalEffect)].HasWork())
			{
				StageToWaitForTemporalEffect = static_cast<ENiagaraGpuComputeTickStage::Type>(static_cast<int32>(StageToWaitForTemporalEffect) + 1);
			}
		}
	}
#endif // WITH_MGPU
}

void NiagaraEmitterInstanceBatcher::PrepareAllTicks(FRHICommandListImmediate& RHICmdList)
{
	for (int iTickStage=0; iTickStage < ENiagaraGpuComputeTickStage::Max; ++iTickStage)
	{
		for (FNiagaraSystemGpuComputeProxy* ComputeProxy : ProxiesPerStage[iTickStage])
		{
			PrepareTicksForProxy(RHICmdList, ComputeProxy, DispatchListPerStage[iTickStage]);
		}
	}
}

void NiagaraEmitterInstanceBatcher::ExecuteTicks(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, ENiagaraGpuComputeTickStage::Type TickStage)
{
#if WITH_MGPU
	if (GNumAlternateFrameRenderingGroups > 1 && TickStage == StageToWaitForTemporalEffect)
	{
		RHICmdList.WaitForTemporalEffect(TemporalEffectName);
	}
#endif // WITH_MGPU

	// Anything to execute for this stage
	FNiagaraGpuDispatchList& DispatchList = DispatchListPerStage[TickStage];
	if ( !DispatchList.HasWork() )
	{
		return;
	}


	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraEmitterInstanceBatcher_ExecuteTicks, TEXT("NiagaraEmitterInstanceBatcher_ExecuteTicks - TickStage(%d)"), TickStage);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraGPUSimTick_RT);
	SCOPED_GPU_STAT(RHICmdList, NiagaraGPUSimulation);

	FUniformBufferStaticBindings GlobalUniformBuffers;
	if (FRHIUniformBuffer* SceneTexturesUniformBuffer = GNiagaraViewDataManager.GetSceneTextureUniformParameters())
	{
		GlobalUniformBuffers.AddUniformBuffer(SceneTexturesUniformBuffer);
	}
	if (FRHIUniformBuffer* MobileSceneTexturesUniformBuffer = GNiagaraViewDataManager.GetMobileSceneTextureUniformParameters())
	{
		GlobalUniformBuffers.AddUniformBuffer(MobileSceneTexturesUniformBuffer);
	}
	SCOPED_UNIFORM_BUFFER_GLOBAL_BINDINGS(RHICmdList, GlobalUniformBuffers);

	FMemMark Mark(FMemStack::Get());
	TArray<FRHITransitionInfo, TMemStackAllocator<>> TransitionsBefore;
	TArray<FRHITransitionInfo, TMemStackAllocator<>> TransitionsAfter;
	TArray<FNiagaraDataBuffer*, TMemStackAllocator<>> IDToIndexInit;

	for ( const FNiagaraGpuDispatchGroup& DispatchGroup : DispatchList.DispatchGroups )
	{
		SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, NiagaraDispatchGroup, NiagaraEmitterInstanceBatcherLocal::GAddDispatchGroupDrawEvent);

		const bool bIsFirstGroup = &DispatchGroup == &DispatchList.DispatchGroups[0];
		const bool bIsLastGroup = &DispatchGroup == &DispatchList.DispatchGroups.Last();

		// Generate transitions and discover free / ID table updates
		TransitionsBefore.Reserve(DispatchGroup.DispatchInstances.Num() * 3);
		TransitionsAfter.Reserve(DispatchGroup.DispatchInstances.Num() * 3);
		for (const FNiagaraGpuDispatchInstance& DispatchInstance : DispatchGroup.DispatchInstances)
		{
			if (FNiagaraDataBuffer* DestinationBuffer = DispatchInstance.SimStageData.Destination)
			{
				NiagaraEmitterInstanceBatcherLocal::AddDataBufferTransitions(TransitionsBefore, TransitionsAfter, DestinationBuffer);
			}

			FNiagaraComputeExecutionContext* ComputeContext = DispatchInstance.InstanceData.Context;
			const bool bRequiresPersistentIDs = ComputeContext->MainDataSet->RequiresPersistentIDs();
			if (bRequiresPersistentIDs)
			{
				if ( FNiagaraDataBuffer* IDtoIndexBuffer = DispatchInstance.SimStageData.Destination )
				{
					IDToIndexInit.Add(IDtoIndexBuffer);
					TransitionsBefore.Emplace(IDtoIndexBuffer->GetGPUIDToIndexTable().UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute);
					TransitionsAfter.Emplace(IDtoIndexBuffer->GetGPUIDToIndexTable().UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute);
				}
			}
		}

		TransitionsBefore.Emplace(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV, bIsFirstGroup ? FNiagaraGPUInstanceCountManager::kCountBufferDefaultState : ERHIAccess::UAVCompute, ERHIAccess::UAVCompute);
		if (bIsLastGroup)
		{
			TransitionsAfter.Emplace(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV, ERHIAccess::UAVCompute, FNiagaraGPUInstanceCountManager::kCountBufferDefaultState);
		}

		if (DispatchGroup.FreeIDUpdates.Num() > 0)
		{
			for (FNiagaraComputeExecutionContext* ComputeContext : DispatchGroup.FreeIDUpdates )
			{
				TransitionsAfter.Emplace(ComputeContext->MainDataSet->GetGPUFreeIDs().UAV, ERHIAccess::SRVCompute, ERHIAccess::UAVCompute);
			}
		}

		// Consume per tick data from the game thread
		//for (FNiagaraGPUSystemTick* Tick : DispatchGroup.TicksWithPerInstanceData)
		//{
		//	uint8* BasePointer = reinterpret_cast<uint8*>(Tick->DIInstanceData->PerInstanceDataForRT);

		//	for (const auto& Pair : Tick->DIInstanceData->InterfaceProxiesToOffsets)
		//	{
		//		FNiagaraDataInterfaceProxy* Proxy = Pair.Key;
		//		uint8* InstanceDataPtr = BasePointer + Pair.Value;
		//		Proxy->ConsumePerInstanceDataFromGameThread(InstanceDataPtr, Tick->SystemInstanceID);
		//	}
		//}

		// Execute Before Transitions
		RHICmdList.Transition(TransitionsBefore);
		TransitionsBefore.Reset();

		// Initialize the IDtoIndex tables
		if (IDToIndexInit.Num() > 0)
		{
			SCOPED_DRAW_EVENT(RHICmdList, NiagaraGPUComputeClearIDToIndexBuffer);

			TArray<FRHITransitionInfo, TMemStackAllocator<>> IDToIndexTransitions;
			IDToIndexTransitions.Reserve(IDToIndexInit.Num());

			for (FNiagaraDataBuffer* IDtoIndexBuffer : IDToIndexInit)
			{
				NiagaraFillGPUIntBuffer(RHICmdList, FeatureLevel, IDtoIndexBuffer->GetGPUIDToIndexTable(), INDEX_NONE);
				IDToIndexTransitions.Emplace(IDtoIndexBuffer->GetGPUIDToIndexTable().UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVCompute);
			}
			IDToIndexInit.Reset();
			RHICmdList.Transition(IDToIndexTransitions);
		}

		// Execute PreStage
		for (const FNiagaraGpuDispatchInstance& DispatchInstance : DispatchGroup.DispatchInstances)
		{
			PreStageInterface(RHICmdList, DispatchInstance.Tick, DispatchInstance.InstanceData, DispatchInstance.SimStageData);
		}

		// Execute Stage
		RHICmdList.BeginUAVOverlap(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);
		for (const FNiagaraGpuDispatchInstance& DispatchInstance : DispatchGroup.DispatchInstances)
		{
			++FNiagaraComputeExecutionContext::TickCounter;
			if (DispatchInstance.InstanceData.bResetData && DispatchInstance.SimStageData.bFirstStage )
			{
				ResetDataInterfaces(RHICmdList, DispatchInstance.Tick, DispatchInstance.InstanceData);
			}
#if STATS
			if (GPUProfiler.IsProfilingEnabled() && DispatchInstance.Tick.bIsFinalTick)
			{
				TStatId StatId;
				if (const FSimulationStageMetaData* StageMetaData = DispatchInstance.SimStageData.StageMetaData)
				{
					StatId = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraDetailed>("GPU_Stage_" + StageMetaData->SimulationStageName.ToString());
				}
				else
				{
					static FString DefaultStageName = "GPU_Stage_SpawnUpdate";
					StatId = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_NiagaraDetailed>(DefaultStageName);
				}
				const int32 TimerHandle = GPUProfiler.StartTimer((uint64)DispatchInstance.InstanceData.Context, StatId, RHICmdList);
				DispatchStage(RHICmdList, ViewUniformBuffer, DispatchInstance.Tick, DispatchInstance.InstanceData, DispatchInstance.SimStageData);
				GPUProfiler.EndTimer(TimerHandle, RHICmdList);
			}
			else
#endif
			{
				DispatchStage(RHICmdList, ViewUniformBuffer, DispatchInstance.Tick, DispatchInstance.InstanceData, DispatchInstance.SimStageData);
			}
		}
		RHICmdList.EndUAVOverlap(GPUInstanceCounterManager.GetInstanceCountBuffer().UAV);

		// Execute PostStage
		for (const FNiagaraGpuDispatchInstance& DispatchInstance : DispatchGroup.DispatchInstances)
		{
			PostStageInterface(RHICmdList, DispatchInstance.Tick, DispatchInstance.InstanceData, DispatchInstance.SimStageData);
			if ( DispatchInstance.SimStageData.bLastStage )
			{
				PostSimulateInterface(RHICmdList, DispatchInstance.Tick, DispatchInstance.InstanceData);

				// Update CurrentData with the latest information as things like ParticleReads can use this data
				FNiagaraComputeExecutionContext* ComputeContext = DispatchInstance.InstanceData.Context;
				const FNiagaraSimStageData& FinalSimStageData = DispatchInstance.SimStageData;
				FNiagaraDataBuffer* FinalSimStageDataBuffer = FinalSimStageData.Destination != nullptr ? FinalSimStageData.Destination : FinalSimStageData.Source;
				check(FinalSimStageDataBuffer != nullptr);

				// If we are setting the data to render we need to ensure we switch back to the original CurrentData then swap the GPU buffers into it
				if (DispatchInstance.SimStageData.bSetDataToRender)
				{
					check(ComputeContext->DataSetOriginalBuffer_RT != nullptr);
					FNiagaraDataBuffer* CurrentData = ComputeContext->DataSetOriginalBuffer_RT;
					ComputeContext->DataSetOriginalBuffer_RT = nullptr;

					ComputeContext->MainDataSet->CurrentData = CurrentData;
					CurrentData->SwapGPU(FinalSimStageDataBuffer);

					ComputeContext->SetTranslucentDataToRender(nullptr);
					ComputeContext->SetDataToRender(CurrentData);
#if WITH_MGPU
					AddTemporalEffectBuffers(CurrentData);
#endif // WITH_MGPU
				}
				// If this is not the final tick of the final stage we need set our temporary buffer for data interfaces, etc, that may snoop from CurrentData
				else
				{
					ComputeContext->MainDataSet->CurrentData = FinalSimStageDataBuffer;
				}
			}
		}

		// Execute After Transitions
		RHICmdList.Transition(TransitionsAfter);
		TransitionsAfter.Reset();

#if WITH_MGPU
		// For PreInitViews we actually need to broadcast after UpdateDrawIndirectBuffer
		// since this is the last place the instance count buffer is written to.
		if (TickStage == StageToBroadcastTemporalEffect && StageToBroadcastTemporalEffect != ENiagaraGpuComputeTickStage::PreInitViews)
		{
			BroadcastTemporalEffect(RHICmdList);
		}
#endif // WITH_MGPU

		// Update free IDs
		if (DispatchGroup.FreeIDUpdates.Num() > 0)
		{
			UpdateFreeIDsListSizesBuffer(RHICmdList, DispatchGroup.FreeIDUpdates.Num());
			UpdateFreeIDBuffers(RHICmdList, DispatchGroup.FreeIDUpdates);

			for (FNiagaraComputeExecutionContext* ComputeContext : DispatchGroup.FreeIDUpdates)
			{
				TransitionsAfter.Emplace(ComputeContext->MainDataSet->GetGPUFreeIDs().UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVCompute);
			}
			RHICmdList.Transition(TransitionsAfter);
			TransitionsAfter.Reset();
		}
	}

	DispatchList.DispatchGroups.Empty();
	if (DispatchList.CountsToRelease.Num() > 0)
	{
		GPUInstanceCounterManager.FreeEntryArray(DispatchList.CountsToRelease);
		DispatchList.CountsToRelease.Empty();
	}
}

void NiagaraEmitterInstanceBatcher::DispatchStage(FRHICommandList& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraSimStageData& SimStageData)
{
	// Setup source buffer
	if ( SimStageData.Source != nullptr )
	{
		SimStageData.Source->SetNumInstances(SimStageData.SourceNumInstances);
		SimStageData.Source->GPUInstanceCountBufferOffset = SimStageData.SourceCountOffset;
	}

	// Setup destination buffer
	int32 InstancesToSpawn = 0;
	if ( SimStageData.Destination != nullptr )
	{
		SimStageData.Destination->SetNumInstances(SimStageData.DestinationNumInstances);
		SimStageData.Destination->GPUInstanceCountBufferOffset = SimStageData.DestinationCountOffset;
		SimStageData.Destination->SetIDAcquireTag(FNiagaraComputeExecutionContext::TickCounter);

		if ( SimStageData.bFirstStage )
		{
			check(SimStageData.DestinationNumInstances >= SimStageData.SourceNumInstances);
			InstancesToSpawn = SimStageData.DestinationNumInstances - SimStageData.SourceNumInstances;
		}
		SimStageData.Destination->SetNumSpawnedInstances(InstancesToSpawn);
	}

	// Get dispatch count
	uint32 DispatchCount = 0;
	if ( SimStageData.AlternateIterationSource )
	{
		const FIntVector ElementCount = SimStageData.AlternateIterationSource->GetElementCount(Tick.SystemInstanceID);
		// Verify the number of elements isn't higher that what we can handle
		checkf(ElementCount.X * ElementCount.Y * ElementCount.Z < uint64(TNumericLimits<uint32>::Max()), TEXT("ElementCount(%d, %d, %d) for IterationInterface(%s) overflows an int32 this is not allowed"), ElementCount.X, ElementCount.Y, ElementCount.Z, *SimStageData.AlternateIterationSource->SourceDIName.ToString());

		DispatchCount = ElementCount.X * ElementCount.Y * ElementCount.Z;
	}
	else
	{
		DispatchCount = SimStageData.DestinationNumInstances;
	}

	if ( DispatchCount == 0 )
	{
		return;
	}

	// Get Shader
	const int32 PermutationId = Tick.NumInstancesWithSimStages > 0 ? InstanceData.Context->GPUScript_RT->ShaderStageIndexToPermutationId_RenderThread(SimStageData.StageIndex) : 0;
	const FNiagaraShaderRef ComputeShader = InstanceData.Context->GPUScript_RT->GetShader(PermutationId);
	FRHIComputeShader* RHIComputeShader = ComputeShader.GetComputeShader();
	RHICmdList.SetComputeShader(RHIComputeShader);

	SCOPED_DRAW_EVENTF(RHICmdList, NiagaraGPUSimulationCS,
		TEXT("NiagaraGpuSim(%s) DispatchCount(%u) Stage(%s %u) NumInstructions(%u)"),
		InstanceData.Context->GetDebugSimName(),
		DispatchCount,
		SimStageData.StageMetaData ? *SimStageData.StageMetaData->SimulationStageName.ToString() : TEXT("Particles"),
		SimStageData.StageIndex,
		ComputeShader->GetNumInstructions()
	);
	FNiagaraUAVPoolAccessScope UAVPoolAccessScope(*this);

	// Set Parameters
	const bool bRequiresPersistentIDs = InstanceData.Context->MainDataSet->RequiresPersistentIDs();

	SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->SimStartParam, InstanceData.bResetData ? 1U : 0U);
	SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->EmitterTickCounterParam, FNiagaraComputeExecutionContext::TickCounter);
	SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->UpdateStartInstanceParam, 0);
	SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->NumSpawnedInstancesParam, InstancesToSpawn);
	SetSRVParameter(RHICmdList, RHIComputeShader, ComputeShader->FreeIDBufferParam, bRequiresPersistentIDs ? InstanceData.Context->MainDataSet->GetGPUFreeIDs().SRV.GetReference() : FNiagaraRenderer::GetDummyIntBuffer());

	//-TODO: REMOVE OLD SIM STAGES
	SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->DefaultSimulationStageIndexParam, 0);
	SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->SimulationStageIndexParam, SimStageData.StageIndex);
	//-TODO: REMOVE OLD SIM STAGES

	// Set spawn Information
	// This parameter is an array of structs with 2 floats and 2 ints on CPU, but a float4 array on GPU. The shader uses asint() to cast the integer values. To set the parameter,
	// we pass the structure array as a float* to SetShaderValueArray() and specify the number of floats (not float vectors).
	static_assert((sizeof(InstanceData.SpawnInfo.SpawnInfoStartOffsets) % SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) == 0, "sizeof SpawnInfoStartOffsets should be a multiple of SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT");
	static_assert((sizeof(InstanceData.SpawnInfo.SpawnInfoParams) % SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT) == 0, "sizeof SpawnInfoParams should be a multiple of SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT");
	SetShaderValueArray(RHICmdList, RHIComputeShader, ComputeShader->EmitterSpawnInfoOffsetsParam, InstanceData.SpawnInfo.SpawnInfoStartOffsets, NIAGARA_MAX_GPU_SPAWN_INFOS);
	SetShaderValueArray(RHICmdList, RHIComputeShader, ComputeShader->EmitterSpawnInfoParamsParam, &InstanceData.SpawnInfo.SpawnInfoParams[0].IntervalDt, 4 * NIAGARA_MAX_GPU_SPAWN_INFOS);

	if (ComputeShader->ViewUniformBufferParam.IsBound())
	{
		if (ensureMsgf(ViewUniformBuffer != nullptr, TEXT("ViewUniformBuffer is required for '%s' but we do not have one to bind"), InstanceData.Context->GetDebugSimName()))
		{
			RHICmdList.SetShaderUniformBuffer(RHIComputeShader, ComputeShader->ViewUniformBufferParam.GetBaseIndex(), ViewUniformBuffer);
		}
	}

	// Setup instance counts
	if (ComputeShader->InstanceCountsParam.IsBound())
	{
		ComputeShader->InstanceCountsParam.SetBuffer(RHICmdList, RHIComputeShader, GPUInstanceCounterManager.GetInstanceCountBuffer());

		if (SimStageData.AlternateIterationSource)
		{
			SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->ReadInstanceCountOffsetParam, -1);
			SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->WriteInstanceCountOffsetParam, -1);
		}
		else
		{
			SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->ReadInstanceCountOffsetParam, SimStageData.SourceCountOffset);
			SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->WriteInstanceCountOffsetParam, SimStageData.DestinationCountOffset);
		}
	}

	// Simulation Stage Information
	// X = Count Buffer Instance Count Offset (INDEX_NONE == Use Instance Count)
	// Y = Instance Count
	// Z = Iteration Index
	// W = Num Iterations
	{
		FIntVector4 SimulationStageIterationInfo = { INDEX_NONE, -1, 0, 0 };
		float SimulationStageNormalizedIterationIndex = 0.0f;

		if (SimStageData.AlternateIterationSource != nullptr)
		{
			const uint32 IterationInstanceCountOffset = SimStageData.AlternateIterationSource->GetGPUInstanceCountOffset(Tick.SystemInstanceID);
			SimulationStageIterationInfo.X = IterationInstanceCountOffset;
			SimulationStageIterationInfo.Y = IterationInstanceCountOffset == INDEX_NONE ? DispatchCount : 0;
		}

		if (SimStageData.StageMetaData != nullptr)
		{
			const int32 NumStages = SimStageData.StageMetaData->MaxStage - SimStageData.StageMetaData->MinStage;
			ensure((int32(SimStageData.StageIndex) >= SimStageData.StageMetaData->MinStage) && (int32(SimStageData.StageIndex) < SimStageData.StageMetaData->MaxStage));
			const int32 IterationIndex = SimStageData.StageIndex - SimStageData.StageMetaData->MinStage;
			SimulationStageIterationInfo.Z = SimStageData.StageIndex - SimStageData.StageMetaData->MinStage;
			SimulationStageIterationInfo.W = NumStages;
			SimulationStageNormalizedIterationIndex = NumStages > 1 ? float(IterationIndex) / float(NumStages - 1) : 1.0f;
		}
		SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->SimulationStageIterationInfoParam, SimulationStageIterationInfo);
		SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->SimulationStageNormalizedIterationIndexParam, SimulationStageNormalizedIterationIndex);
	}

	// Set Input & Output buffers
	FNiagaraDataBuffer::SetInputShaderParams(RHICmdList, ComputeShader.GetShader(), SimStageData.Source);
	FNiagaraDataBuffer::SetOutputShaderParams(RHICmdList, ComputeShader.GetShader(), SimStageData.Destination);

	// Set data interface parameters
	SetDataInterfaceParameters(RHICmdList, Tick, InstanceData, ComputeShader, SimStageData);

	// Execute the dispatch
	{
		const uint32 ThreadGroupSize = FNiagaraShader::GetGroupSize(ShaderPlatform);
		checkf(ThreadGroupSize <= NiagaraComputeMaxThreadGroupSize, TEXT("ShaderPlatform(%d) is set to ThreadGroup size (%d) which is > the NiagaraComputeMaxThreadGroupSize(%d)"), ShaderPlatform, ThreadGroupSize, NiagaraComputeMaxThreadGroupSize);
		const uint32 TotalThreadGroups = FMath::DivideAndRoundUp(DispatchCount, ThreadGroupSize);

		const uint32 ThreadGroupCountZ = 1;
		const uint32 ThreadGroupCountY = FMath::DivideAndRoundUp(TotalThreadGroups, NiagaraMaxThreadGroupCountPerDimension);
		const uint32 ThreadGroupCountX = FMath::DivideAndRoundUp(TotalThreadGroups, ThreadGroupCountY);

		SetShaderValue(RHICmdList, RHIComputeShader, ComputeShader->DispatchThreadIdToLinearParam, ThreadGroupCountY > 1 ? ThreadGroupCountX * ThreadGroupSize : 0);

		SetConstantBuffers(RHICmdList, ComputeShader, Tick, &InstanceData);

		DispatchComputeShader(RHICmdList, ComputeShader, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);

		INC_DWORD_STAT(STAT_NiagaraGPUDispatches);
	}

	// Unset UAV parameters
	UnsetDataInterfaceParameters(RHICmdList, Tick, InstanceData, ComputeShader, SimStageData);
	FNiagaraDataBuffer::UnsetShaderParams(RHICmdList, ComputeShader.GetShader());
	ComputeShader->InstanceCountsParam.UnsetUAV(RHICmdList, RHIComputeShader);

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

void NiagaraEmitterInstanceBatcher::PreInitViews(FRHICommandListImmediate& RHICmdList, bool bAllowGPUParticleUpdate)
{
	bRequiresReadback = false;

	FramesBeforeTickFlush = 0;

	GpuReadbackManagerPtr->Tick();
#if NIAGARA_COMPUTEDEBUG_ENABLED
	if ( FNiagaraGpuComputeDebug* GpuComputeDebug = GetGpuComputeDebug() )
	{
		GpuComputeDebug->Tick(RHICmdList);
	}
#endif

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
	if (bAllowGPUParticleUpdate && FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		UpdateInstanceCountManager(RHICmdList);
		PrepareAllTicks(RHICmdList);

		if ( NiagaraEmitterInstanceBatcherLocal::GDebugLogging)
		{
			DumpDebugFrame(RHICmdList);
		}

		ExecuteTicks(RHICmdList, nullptr, ENiagaraGpuComputeTickStage::PreInitViews);
	}
	else
	{
		GPUInstanceCounterManager.ResizeBuffers(RHICmdList, FeatureLevel,  0);
		FinishDispatches();
	}
}

void NiagaraEmitterInstanceBatcher::PostInitViews(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, bool bAllowGPUParticleUpdate)
{
	LLM_SCOPE(ELLMTag::Niagara);

	if (bAllowGPUParticleUpdate && FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		ExecuteTicks(RHICmdList, ViewUniformBuffer, ENiagaraGpuComputeTickStage::PostInitViews);
	}
}

void NiagaraEmitterInstanceBatcher::PostRenderOpaque(FRHICommandListImmediate& RHICmdList, FRHIUniformBuffer* ViewUniformBuffer, const class FShaderParametersMetadata* SceneTexturesUniformBufferStruct, FRHIUniformBuffer* SceneTexturesUniformBuffer, bool bAllowGPUParticleUpdate)
{
	LLM_SCOPE(ELLMTag::Niagara);

	if (bAllowGPUParticleUpdate && FNiagaraUtilities::AllowGPUParticles(GetShaderPlatform()))
	{
		ExecuteTicks(RHICmdList, ViewUniformBuffer, ENiagaraGpuComputeTickStage::PostOpaqueRender);

		FinishDispatches();

		ProcessDebugReadbacks(RHICmdList, false);
	}

	if (!GPUInstanceCounterManager.HasPendingGPUReadback() && bRequiresReadback)
	{
		GPUInstanceCounterManager.EnqueueGPUReadback(RHICmdList);
	}
}

void NiagaraEmitterInstanceBatcher::ProcessDebugReadbacks(FRHICommandListImmediate& RHICmdList, bool bWaitCompletion)
{
#if !UE_BUILD_SHIPPING
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
	GpuDebugReadbackInfos.Empty();

	if (bWaitCompletion)
	{
		GpuReadbackManagerPtr->WaitCompletion(RHICmdList);
	}
#endif
}

bool NiagaraEmitterInstanceBatcher::UsesGlobalDistanceField() const
{
	return NumProxiesThatRequireDistanceFieldData > 0;
}

bool NiagaraEmitterInstanceBatcher::UsesDepthBuffer() const
{
	return NumProxiesThatRequireDepthBuffer > 0;
}

bool NiagaraEmitterInstanceBatcher::RequiresEarlyViewUniformBuffer() const
{
	return NumProxiesThatRequireEarlyViewData > 0;
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
		// Note: We don't care that the buffer will be allowed to be reused
		FNiagaraUAVPoolAccessScope UAVPoolAccessScope(*this);
		Params.OutCulledParticleCounts = GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, ENiagaraEmptyUAVType::Buffer);
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
			Params.MeshIndex = SortInfo.MeshIndex;
			Params.MeshIndexAttributeOffset = SortInfo.MeshIndexAttributeOffset;
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
void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraShaderRef& ComputeShader, const FNiagaraSimStageData& SimStageData) const
{
	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FNiagaraShaderMapPointerTable& PointerTable = ComputeShader.GetPointerTable();

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : InstanceData.DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			FNiagaraDataInterfaceSetArgs Context(Interface, Tick.SystemInstanceID, this, ComputeShader, &InstanceData, &SimStageData, InstanceData.IsOutputStage(Interface, SimStageData.StageIndex), InstanceData.IsIterationStage(Interface, SimStageData.StageIndex));
			DIParam.DIType.Get(PointerTable.DITypes)->SetParameters(DIParam.Parameters.Get(), RHICmdList, Context);
		}

		InterfaceIndex++;
	}
}

void NiagaraEmitterInstanceBatcher::UnsetDataInterfaceParameters(FRHICommandList& RHICmdList, const FNiagaraGPUSystemTick& Tick, const FNiagaraComputeInstanceData& InstanceData, const FNiagaraShaderRef& ComputeShader, const FNiagaraSimStageData& SimStageData) const
{
	// @todo-threadsafety This is a bit gross. Need to rethink this api.
	const FNiagaraShaderMapPointerTable& PointerTable = ComputeShader.GetPointerTable();

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : InstanceData.DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = ComputeShader->GetDIParameters()[InterfaceIndex];
		if (DIParam.Parameters.IsValid())
		{
			FNiagaraDataInterfaceSetArgs Context(Interface, Tick.SystemInstanceID, this, ComputeShader, &InstanceData, &SimStageData, InstanceData.IsOutputStage(Interface, SimStageData.StageIndex), InstanceData.IsIterationStage(Interface, SimStageData.StageIndex));
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

FGPUSortManager* NiagaraEmitterInstanceBatcher::GetGPUSortManager() const
{
	return GPUSortManager;
}

#if !UE_BUILD_SHIPPING
void NiagaraEmitterInstanceBatcher::AddDebugReadback(FNiagaraSystemInstanceID InstanceID, TSharedPtr<struct FNiagaraScriptDebuggerInfo, ESPMode::ThreadSafe> DebugInfo, FNiagaraComputeExecutionContext* Context)
{
	FDebugReadbackInfo& ReadbackInfo = GpuDebugReadbackInfos.AddDefaulted_GetRef();
	ReadbackInfo.InstanceID = InstanceID;
	ReadbackInfo.DebugInfo = DebugInfo;
	ReadbackInfo.Context = Context;
}
#endif

NiagaraEmitterInstanceBatcher::FDummyUAV::~FDummyUAV()
{
	UAV.SafeRelease();
	Buffer.SafeRelease();
	Texture.SafeRelease();
}

void NiagaraEmitterInstanceBatcher::FDummyUAV::Init(FRHICommandList& RHICmdList, EPixelFormat Format, ENiagaraEmptyUAVType Type, const TCHAR* DebugName)
{
	checkSlow(IsInRenderingThread());

	FRHIResourceCreateInfo CreateInfo;
	CreateInfo.DebugName = DebugName;

	switch(Type)
	{
		case ENiagaraEmptyUAVType::Buffer:
		{
			uint32 BytesPerElement = GPixelFormats[Format].BlockBytes;
			Buffer = RHICreateVertexBuffer(BytesPerElement, BUF_UnorderedAccess | BUF_ShaderResource, CreateInfo);
			UAV = RHICreateUnorderedAccessView(Buffer, Format);
			break;
		}

		case ENiagaraEmptyUAVType::Texture2D:
		{
			Texture = RHICreateTexture2D(1, 1, Format, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
			UAV = RHICreateUnorderedAccessView(Texture, 0);
			break;
		}

		case ENiagaraEmptyUAVType::Texture2DArray:
		{
			Texture = RHICreateTexture2DArray(1, 1, 1, Format, 1, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
			UAV = RHICreateUnorderedAccessView(Texture, 0);
			break;
		}

		case ENiagaraEmptyUAVType::Texture3D:
		{
			Texture = RHICreateTexture3D(1, 1, 1, Format, 1, TexCreate_ShaderResource | TexCreate_UAV, CreateInfo);
			UAV = RHICreateUnorderedAccessView(Texture, 0);
			break;
		}

		default:
		{
			checkNoEntry();
			return;
		}
	}

	RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
}

NiagaraEmitterInstanceBatcher::FDummyUAVPool::~FDummyUAVPool()
{
	UE_CLOG(NextFreeIndex != 0, LogNiagara, Warning, TEXT("DummyUAVPool is potentially in use during destruction."));
	DEC_DWORD_STAT_BY(STAT_NiagaraDummyUAVPool, UAVs.Num());
}

FRHIUnorderedAccessView* NiagaraEmitterInstanceBatcher::GetEmptyUAVFromPool(FRHICommandList& RHICmdList, EPixelFormat Format, ENiagaraEmptyUAVType Type) const
{
	checkf(DummyUAVAccessCounter != 0, TEXT("Accessing Niagara's UAV Pool while not within a scope, this could result in a memory leak!"));

	TMap<EPixelFormat, FDummyUAVPool>& UAVMap = DummyUAVPools[(int)Type];
	FDummyUAVPool& Pool = UAVMap.FindOrAdd(Format);
	checkSlow(Pool.NextFreeIndex <= Pool.UAVs.Num());
	if (Pool.NextFreeIndex == Pool.UAVs.Num())
	{
		FDummyUAV& NewUAV = Pool.UAVs.AddDefaulted_GetRef();
		NewUAV.Init(RHICmdList, Format, Type, TEXT("NiagaraEmitterInstanceBatcher::DummyUAV"));
		// Dispatches which use dummy UAVs are allowed to overlap, since we don't care about the contents of these buffers.
		// We never need to calll EndUAVOverlap() on these.
		RHICmdList.BeginUAVOverlap(NewUAV.UAV);

		INC_DWORD_STAT(STAT_NiagaraDummyUAVPool);
	}

	FRHIUnorderedAccessView* UAV = Pool.UAVs[Pool.NextFreeIndex].UAV;
	++Pool.NextFreeIndex;
	return UAV;
}

void NiagaraEmitterInstanceBatcher::ResetEmptyUAVPools()
{
	for (int Type = 0; Type < UE_ARRAY_COUNT(DummyUAVPools); ++Type)
	{
		for (TPair<EPixelFormat, FDummyUAVPool>& Entry : DummyUAVPools[Type])
		{
			Entry.Value.NextFreeIndex = 0;
		}
	}
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

#if WITH_MGPU
void NiagaraEmitterInstanceBatcher::AddTemporalEffectBuffers(FNiagaraDataBuffer* FinalData)
{
	if (GNumAlternateFrameRenderingGroups > 1)
	{
		for (FRHIVertexBuffer* Buffer : { FinalData->GetGPUBufferFloat().Buffer, FinalData->GetGPUBufferInt().Buffer, FinalData->GetGPUBufferHalf().Buffer })
		{
			if (Buffer)
			{
				TemporalEffectBuffers.Add(Buffer);
			}
		}
	}
}

void NiagaraEmitterInstanceBatcher::BroadcastTemporalEffect(FRHICommandList& RHICmdList)
{
	if (GNumAlternateFrameRenderingGroups > 1)
	{
		if (GPUInstanceCounterManager.GetInstanceCountBuffer().Buffer)
		{
			TemporalEffectBuffers.Add(GPUInstanceCounterManager.GetInstanceCountBuffer().Buffer);
		}
		if (TemporalEffectBuffers.Num())
		{
			RHICmdList.BroadcastTemporalEffect(TemporalEffectName, TemporalEffectBuffers);
			TemporalEffectBuffers.Reset();
		}
	}
}
#endif // WITH_MGPU