// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponent.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshBatch.h"
#include "NiagaraCommon.h"
#include "NiagaraComponentSettings.h"
#include "NiagaraConstants.h"
#include "NiagaraCrashReporterHandler.h"
#include "NiagaraCustomVersion.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraRenderer.h"
#include "NiagaraStats.h"
#include "NiagaraSystem.h"
#include "NiagaraWorldManager.h"
#include "PrimitiveSceneInfo.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UObject/NameTypes.h"
#include "VectorVM.h"
#include "Engine/StaticMesh.h"

DECLARE_CYCLE_STAT(TEXT("Sceneproxy create (GT)"), STAT_NiagaraCreateSceneProxy, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Component Tick (GT)"), STAT_NiagaraComponentTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Activate (GT)"), STAT_NiagaraComponentActivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Deactivate (GT)"), STAT_NiagaraComponentDeactivate, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Send Render Data (GT)"), STAT_NiagaraComponentSendRenderData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Set Dynamic Data (RT)"), STAT_NiagaraSetDynamicData, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Get Dynamic Mesh Elements (RT)"), STAT_NiagaraComponentGetDynamicMeshElements, STATGROUP_Niagara);

DEFINE_LOG_CATEGORY(LogNiagara);

static int GNiagaraSoloTickEarly = 1;
static FAutoConsoleVariableRef CVarNiagaraSoloTickEarly(
	TEXT("fx.Niagara.Solo.TickEarly"),
	GNiagaraSoloTickEarly,
	TEXT("When enabled will tick kin the first available tick group."),
	ECVF_Default
);

static int GNiagaraSoloAllowAsyncWorkToEndOfFrame = 1;
static FAutoConsoleVariableRef CVarNiagaraSoloAllowAsyncWorkToEndOfFrame(
	TEXT("fx.Niagara.Solo.AllowAsyncWorkToEndOfFrame"),
	GNiagaraSoloAllowAsyncWorkToEndOfFrame,
	TEXT("Allow async work to continue until the end of the frame for solo Niagara instances, if false it will complete within the tick group it started in."),
	ECVF_Default
);

static int32 GbSuppressNiagaraSystems = 0;
static FAutoConsoleVariableRef CVarSuppressNiagaraSystems(
	TEXT("fx.SuppressNiagaraSystems"),
	GbSuppressNiagaraSystems,
	TEXT("If > 0 Niagara particle systems will not be activated. \n"),
	ECVF_Default
);

static int32 GNiagaraComponentWarnNullAsset = 0;
static FAutoConsoleVariableRef CVarNiagaraComponentWarnNullAsset(
	TEXT("fx.Niagara.ComponentWarnNullAsset"),
	GNiagaraComponentWarnNullAsset,
	TEXT("When enabled we will warn if a NiagaraComponent is activate with a null asset.  This is sometimes useful for tracking down components that can be removed."),
	ECVF_Default
);

static int32 GNiagaraComponentWarnAsleepCullReaction = 1;
static FAutoConsoleVariableRef CVarNiagaraComponentWarnAsleepCullReaction(
	TEXT("fx.Niagara.ComponentWarnAsleepCullReaction"),
	GNiagaraComponentWarnAsleepCullReaction,
	TEXT("When enabled we will warn if a NiagaraComponent completes naturally but has Asleep mode set for cullreaction."),
	ECVF_Default
);

static int32 GNiagaraUseFastSetUserParametersToDefaultValues = 1;
static FAutoConsoleVariableRef CVarNiagaraUseFastSetUserParametersToDefaultValues(
	TEXT("fx.Niagara.UseFastSetUserParametersToDefaultValues"),
	GNiagaraUseFastSetUserParametersToDefaultValues,
	TEXT("When a component is activated we will check the surpession list."),
	ECVF_Default
);

static int GNiagaraForceWaitForCompilationOnActivate = 0;
static FAutoConsoleVariableRef CVarNiagaraForceWaitForCompilationOnActivate(
	TEXT("fx.Niagara.ForceWaitForCompilationOnActivate"),
	GNiagaraForceWaitForCompilationOnActivate,
	TEXT("When a component is activated it will stall waiting for any pending shader compilation."),
	ECVF_Default
);

FAutoConsoleCommandWithWorldAndArgs DumpNiagaraComponentsCommand(
	TEXT("fx.Niagara.DumpComponents"),
	TEXT("Dump Information about all Niagara Components"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			const bool bFullDump = Args.Contains(TEXT("full"));
			const bool bAllWorlds = (World == nullptr) || Args.Contains(TEXT("allworlds"));
			const FString SystemFilter = 
				[&]() -> FString
				{
					static const FString FilterPrefix(TEXT("filter="));
					for ( const FString& Arg : Args )
					{
						if ( Arg.StartsWith(FilterPrefix) )
						{
							return Arg.Mid(FilterPrefix.Len());
						}
					}
					return FString();
				}();

			UE_LOG(LogNiagara, Log, TEXT("=========================== Begin Niagara Dump ==========================="));
			for (TObjectIterator<UNiagaraComponent> It; It; ++It)
			{
				UNiagaraComponent* Component = *It;
				if ( Component->IsPendingKill() )
				{
					continue;
				}

				// Filter by world
				if (!bAllWorlds && (Component->GetWorld() != World) )
				{
					continue;
				}

				// Filter by asset name, we allow null assets to continue so we can identify components who are attached but have no valid asset associated with them
				UNiagaraSystem* NiagaraSystem = Component->GetAsset();
				if ( !SystemFilter.IsEmpty() )
				{
					if (!NiagaraSystem || !NiagaraSystem->GetName().Contains(SystemFilter) )
					{
						continue;
					}
				}

				UE_LOG(LogNiagara, Log, TEXT("Component '%s' Asset '%s' Actor '%s' is %s"), *GetNameSafe(Component), *GetNameSafe(NiagaraSystem), *GetNameSafe(Component->GetTypedOuter<AActor>()), Component->IsActive() ? TEXT("Active") : TEXT("Inactive"));
				if (FNiagaraSystemInstanceControllerPtr SystemInstanceController = Component->GetSystemInstanceController())
				{
					SystemInstanceController->DebugDump(bFullDump);
				}
			}
			UE_LOG(LogNiagara, Log, TEXT("=========================== End Niagara Dump ==========================="));
		}
	)
);

FNiagaraSceneProxy::FNiagaraSceneProxy(UNiagaraComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent, InComponent->GetAsset() ? InComponent->GetAsset()->GetFName() : FName())
	, RuntimeCycleCount(nullptr)
#if WITH_PARTICLE_PERF_STATS
	, PerfStatsContext(InComponent->GetPerfStatsContext())
#endif
#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
	, PreviewLODDistance(-1.0f)
#endif
{
	if (FNiagaraSystemInstanceControllerConstPtr SystemInstanceController = InComponent->GetSystemInstanceController())
	{
		LLM_SCOPE(ELLMTag::Niagara);

		RenderData = SystemInstanceController->CreateSystemRenderData(GetScene().GetFeatureLevel());
		Batcher = SystemInstanceController->GetBatcher();

		bAlwaysHasVelocity = RenderData->HasAnyMotionBlurEnabled();

#if STATS
		SystemStatID = InComponent->GetAsset()->GetStatID(false, false);
#endif
	}
}

SIZE_T FNiagaraSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FNiagaraSceneProxy::DestroyRenderState_Concurrent()
{
	if (RenderData)
	{
		RenderData->DestroyRenderState_Concurrent();
	}
}

FNiagaraSceneProxy::~FNiagaraSceneProxy()
{
	check(IsInRenderingThread());
	UniformBufferNoVelocity.ReleaseResource();
	delete RenderData;
}

void FNiagaraSceneProxy::ReleaseRenderThreadResources()
{
	if (RenderData)
	{
		RenderData->ReleaseRenderThreadResources();
	}
	UniformBufferNoVelocity.ReleaseResource();
}

// FPrimitiveSceneProxy interface.
void FNiagaraSceneProxy::CreateRenderThreadResources()
{
	if (RenderData)
	{
		LLM_SCOPE(ELLMTag::Niagara);
		RenderData->CreateRenderThreadResources(*Batcher);
	}
}

void FNiagaraSceneProxy::OnTransformChanged()
{
	LocalToWorldInverse = GetLocalToWorld().Inverse();
	UniformBufferNoVelocity.ReleaseResource();
}

FPrimitiveViewRelevance FNiagaraSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;

	if (!GetRenderingEnabled() || !FNiagaraUtilities::SupportsNiagaraRendering(View->GetFeatureLevel()))
	{
		return Relevance;
	}
	Relevance.bDynamicRelevance = true;

	Relevance.bRenderCustomDepth = ShouldRenderCustomDepth();
	Relevance.bDrawRelevance = IsShown(View) && View->Family->EngineShowFlags.Particles && View->Family->EngineShowFlags.Niagara;
	Relevance.bShadowRelevance = IsShadowCast(View);
	Relevance.bRenderInMainPass = ShouldRenderInMainPass();
	Relevance.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Relevance.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;

	Relevance |= RenderData->GetViewRelevance(*View, *this);

	Relevance.bVelocityRelevance = DrawsVelocity() && Relevance.bOpaque && Relevance.bRenderInMainPass;

	return Relevance;
}

FRHIUniformBuffer* FNiagaraSceneProxy::GetUniformBufferNoVelocity() const
{
	if ( !UniformBufferNoVelocity.IsInitialized() )
	{
		bool bHasPrecomputedVolumetricLightmap;
		FMatrix PreviousLocalToWorld;
		int32 SingleCaptureIndex;
		bool bOutputVelocity;
		FPrimitiveSceneInfo* LocalPrimitiveSceneInfo = GetPrimitiveSceneInfo();
		GetScene().GetPrimitiveUniformShaderParameters_RenderThread(LocalPrimitiveSceneInfo, bHasPrecomputedVolumetricLightmap, PreviousLocalToWorld, SingleCaptureIndex, bOutputVelocity);

		UniformBufferNoVelocity.SetContents(
			GetPrimitiveUniformShaderParameters(
				GetLocalToWorld(),
				PreviousLocalToWorld,
				GetActorPosition(),
				GetBounds(),
				GetLocalBounds(),
				GetLocalBounds(),
				ReceivesDecals(),
				HasDistanceFieldRepresentation(),
				HasDynamicIndirectShadowCasterRepresentation(),
				UseSingleSampleShadowFromStationaryLights(),
				bHasPrecomputedVolumetricLightmap,
				DrawsVelocity(),
				GetLightingChannelMask(),
				LocalPrimitiveSceneInfo ? LocalPrimitiveSceneInfo->GetLightmapDataOffset() : 0,
				GetLightMapCoordinateIndex(),
				SingleCaptureIndex,
				false,
				GetCustomPrimitiveData(),
				true,
				LocalPrimitiveSceneInfo ? LocalPrimitiveSceneInfo->GetInstanceDataOffset() : INDEX_NONE,
				LocalPrimitiveSceneInfo ? LocalPrimitiveSceneInfo->GetNumInstanceDataEntries() : 0,
				true
			)
		);
		UniformBufferNoVelocity.InitResource();
	}
	return UniformBufferNoVelocity.GetUniformBufferRHI();
}

uint32 FNiagaraSceneProxy::GetMemoryFootprint() const
{ 
	return (sizeof(*this) + GetAllocatedSize()); 
}

uint32 FNiagaraSceneProxy::GetAllocatedSize() const
{ 
	uint32 Size = FPrimitiveSceneProxy::GetAllocatedSize();
	if (RenderData)
	{
		Size += RenderData->GetDynamicDataSize();
	}
	return Size;
}

bool FNiagaraSceneProxy::GetRenderingEnabled() const
{
	return RenderData ? RenderData->IsRenderingEnabled() : false;
}

void FNiagaraSceneProxy::SetRenderingEnabled(bool bInRenderingEnabled)
{
	if (RenderData)
	{
		RenderData->SetRenderingEnabled(bInRenderingEnabled);
	}
}

void FNiagaraSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_RT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentGetDynamicMeshElements);

#if STATS
	FScopeCycleCounter SystemStatCounter(SystemStatID);
#endif

	if (RenderData)
	{
		RenderData->GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector, *this);
	}

	if (ViewFamily.EngineShowFlags.Particles && ViewFamily.EngineShowFlags.Niagara)
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			if (VisibilityMap & (1 << ViewIndex))
			{
				RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetBounds(), IsSelected());
				if (HasCustomOcclusionBounds())
				{
					RenderBounds(Collector.GetPDI(ViewIndex), ViewFamily.EngineShowFlags, GetCustomOcclusionBounds(), IsSelected());
				}
			}
		}
	}
}

#if RHI_RAYTRACING
void FNiagaraSceneProxy::GetDynamicRayTracingInstances(FRayTracingMaterialGatheringContext& Context, TArray<FRayTracingInstance>& OutRayTracingInstances)
{
if (RenderData)
	{
		RenderData->GetDynamicRayTracingInstances(Context, OutRayTracingInstances, *this);
	}
}
#endif

void FNiagaraSceneProxy::GatherSimpleLights(const FSceneViewFamily& ViewFamily, FSimpleLightArray& OutParticleLights) const
{
	if (RenderData)
	{
		RenderData->GatherSimpleLights(OutParticleLights);
	}
}

//////////////////////////////////////////////////////////////////////////

UNiagaraComponent::UNiagaraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bForceSolo(false)
	, AgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime)
	, DesiredAge(0.0f)
	, LastHandledDesiredAge(0.0f)
	, bCanRenderWhileSeeking(true)
	, SeekDelta(1 / 30.0f)
	, bLockDesiredAgeDeltaTimeToSeekDelta(true)
	, MaxSimTime(33.0f / 1000.0f)
	, bIsSeeking(false)
	, bAutoDestroy(false)
	, MaxTimeBeforeForceUpdateTransform(5.0f)
#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
	, PreviewLODDistance(0.0f)
	, bEnablePreviewLODDistance(false)
#endif
#if WITH_EDITORONLY_DATA
	, bWaitForCompilationOnActivate(false)
#endif
	, bAwaitingActivationDueToNotReady(false)
	, bActivateShouldResetWhenReady(false)
	, bDidAutoAttach(false)
	, bAllowScalability(true)
	, bIsCulledByScalability(false)
	, bDuringUpdateContextReset(false)
	//, bIsChangingAutoAttachment(false)
	, ScalabilityManagerHandle(INDEX_NONE)
	, ForceUpdateTransformTime(0.0f)
	, CurrLocalBounds(ForceInit)
{
	OverrideParameters.SetOwner(this);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = GNiagaraSoloTickEarly ? TG_PrePhysics : TG_DuringPhysics;
	PrimaryComponentTick.EndTickGroup = GNiagaraSoloAllowAsyncWorkToEndOfFrame ? TG_LastDemotable : ETickingGroup(PrimaryComponentTick.TickGroup);
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.SetTickFunctionEnable(false);
	bTickInEditor = true;
	bAutoActivate = true;
	bRenderingEnabled = true;
	SavedAutoAttachRelativeScale3D = FVector(1.f, 1.f, 1.f);

	SetGenerateOverlapEvents(false);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);

	// NIAGARA_WIP - BEGIN: Needs to be disable via version to avoid breaking existing content
	// Disable receiving decals by default.
	bReceivesDecals = false;
	// NIAGARA_WIP - END
}

/********* UFXSystemComponent *********/
void UNiagaraComponent::SetBoolParameter(FName Parametername, bool Param)
{
	SetVariableBool(Parametername, Param);
}

void UNiagaraComponent::SetIntParameter(FName ParameterName, int Param)
{
	SetVariableInt(ParameterName, Param);
}

void UNiagaraComponent::SetFloatParameter(FName ParameterName, float Param)
{
	SetVariableFloat(ParameterName, Param);
}

void UNiagaraComponent::SetVectorParameter(FName ParameterName, FVector Param)
{
	SetVariableVec3(ParameterName, Param);
}

void UNiagaraComponent::SetColorParameter(FName ParameterName, FLinearColor Param)
{
	SetVariableLinearColor(ParameterName, Param);
}

void UNiagaraComponent::SetActorParameter(FName ParameterName, class AActor* Param)
{
	SetVariableActor(ParameterName, Param);
}

UFXSystemAsset* UNiagaraComponent::GetFXSystemAsset() const
{
	return Asset;
}

void UNiagaraComponent::InitForPerformanceBaseline()
{
	bNeverDistanceCull = true;
	SetAllowScalability(false);
	SetPreviewLODDistance(true, 1.0f);
}

void UNiagaraComponent::SetEmitterEnable(FName EmitterName, bool bNewEnableState)
{
	if (SystemInstanceController.IsValid() && !SystemInstanceController->IsComplete())
	{
		SystemInstanceController->SetEmitterEnable(EmitterName, bNewEnableState);
	}
}

void UNiagaraComponent::ReleaseToPool()
{
	// A component may be marked pending kill before the owner has it's reference set to null.
	// In that case there's a window where it can be released back into the pool incorrectly, so we just skip releasing as we know it will be deleted shortly
	if ( IsPendingKillOrUnreachable() )
	{
		return;
	}

	if (PoolingMethod != ENCPoolMethod::ManualRelease)
	{		
		if (UNiagaraComponentPool::Enabled())//Only emit this warning if pooling is enabled. If it's not, all components will have PoolingMethod none.
		{	
			UE_LOG(LogNiagara, Warning, TEXT("Manually releasing a PSC to the pool that was not spawned with ENCPoolMethod::ManualRelease. Asset=%s Component=%s"),
				Asset ? *Asset->GetPathName() : TEXT("NULL"), *GetPathName()
			);
		}
		return;
	}

	if (!IsActive())
	{
		UnregisterWithScalabilityManager();

		//If we're already complete then release to the pool straight away.
		UWorld* World = GetWorld();
		check(World);
		if( FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World))
		{
			WorldMan->GetComponentPool()->ReclaimWorldParticleSystem(this);
		}
		else
		{
			DestroyComponent();
		}
	}
	else
	{
		//If we haven't completed, deactivate and defer release to pool.
		PoolingMethod = ENCPoolMethod::ManualRelease_OnComplete;
		Deactivate();
	}
}

uint32 UNiagaraComponent::GetApproxMemoryUsage() const
{
	// TODO: implement memory usage for the component pool statistics
	return 1;
}

void UNiagaraComponent::ActivateSystem(bool bFlagAsJustAttached)
{
	// Attachment is handled different in niagara so the bFlagAsJustAttached is ignored here.
	if (IsActive())
	{
		// If the system is already active then activate with reset to reset the system simulation but
		// leave the emitter simulations active.
		bool bResetSystem = true;
		bool bIsFromScalability = false;
		ActivateInternal(bResetSystem, bIsFromScalability);
	}
	else
	{
		// Otherwise just follow the standard activate path.
		Activate();
	}
}

/********* UFXSystemComponent *********/


void UNiagaraComponent::TickComponent(float DeltaSeconds, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	LLM_SCOPE(ELLMTag::Niagara);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentTick);

	FScopeCycleCounter SystemStatCounter(Asset ? Asset->GetStatID(true, false) : TStatId());

	if (bAwaitingActivationDueToNotReady)
	{
		Activate(bActivateShouldResetWhenReady);
		return;
	}

	if (!SystemInstanceController)
	{
		return;
	}

	if (!IsActive() && bAutoActivate && SystemInstanceController->GetAreDataInterfacesInitialized())
	{
		Activate();
	}

	check(SystemInstanceController->IsSolo());
	if (IsActive() && SystemInstanceController.Get() && !SystemInstanceController->IsComplete())
	{
		check(Asset != nullptr);
		Asset->AddToInstanceCountStat(1, true);
		INC_DWORD_STAT_BY(STAT_TotalNiagaraSystemInstances, 1);
		INC_DWORD_STAT_BY(STAT_TotalNiagaraSystemInstancesSolo, 1);

		// If the interfaces have changed in a meaningful way, we need to potentially rebind and update the values.
		if (OverrideParameters.GetInterfacesDirty())
		{
			SystemInstanceController->Reset(FNiagaraSystemInstance::EResetMode::ReInit);
		}

		if (AgeUpdateMode == ENiagaraAgeUpdateMode::TickDeltaTime)
		{
			SystemInstanceController->ManualTick(DeltaSeconds * CustomTimeDilation, (ThisTickFunction && ThisTickFunction->IsCompletionHandleValid()) ? ThisTickFunction->GetCompletionHandle() : nullptr);
		}
		else if(AgeUpdateMode == ENiagaraAgeUpdateMode::DesiredAge)
		{
			float AgeDiff = FMath::Max(DesiredAge, 0.0f) - SystemInstanceController->GetAge();
			int32 TicksToProcess = 0;
			if (bLockDesiredAgeDeltaTimeToSeekDelta && FMath::Abs(AgeDiff) < KINDA_SMALL_NUMBER)
			{
				AgeDiff = 0.0f;
			}
			else
			{
				if (AgeDiff < 0.0f)
				{
					SystemInstanceController->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
					AgeDiff = DesiredAge - SystemInstanceController->GetAge();
				}

				if (AgeDiff > 0.0f)
				{
					FNiagaraSystemSimulation* SystemSim = SystemInstanceController->GetSoloSystemSimulation().Get();
					if (SystemSim)
					{
						if (bLockDesiredAgeDeltaTimeToSeekDelta || AgeDiff > SeekDelta)
						{
							// If we're locking the delta time to the seek delta, or we need to seek more than a frame, tick the simulation by the seek delta.
							double StartTime = FPlatformTime::Seconds();
							double CurrentTime = StartTime;

							TicksToProcess = FMath::FloorToInt(AgeDiff / SeekDelta);
							for (; TicksToProcess > 0 && CurrentTime - StartTime < MaxSimTime; --TicksToProcess)
							{
								//Cannot do multiple tick off the game thread here without additional work. So we pass in null for the completion event which will force GT execution.
								//If this becomes a perf problem I can add a new path for the tick code to handle multiple ticks.
								SystemInstanceController->ManualTick(SeekDelta, nullptr);
								CurrentTime = FPlatformTime::Seconds();
							}
						}
						else
						{
							// Otherwise just tick by the age difference.
							SystemInstanceController->ManualTick(AgeDiff, nullptr);
						}
					}
				}
			}

			if (TicksToProcess == 0)
			{
				bIsSeeking = false;
			}
		}
		else if (AgeUpdateMode == ENiagaraAgeUpdateMode::DesiredAgeNoSeek)
		{
			int32 MaxForwardFrames = 5; // HACK - for some reason sequencer jumps forwards by multiple frames on pause, so this is being added to allow for FX to stay alive when being controlled by sequencer in the editor.  This should be lowered once that issue is fixed.
			float AgeDiff = DesiredAge - LastHandledDesiredAge;
			if (AgeDiff < 0)
			{
				if (FMath::Abs(AgeDiff) >= SeekDelta)
				{
					// When going back in time for a frame or more, reset and simulate a single frame.  We ignore small negative changes to delta
					// time which can happen when controlling time with the timeline and the time snaps to a previous time when paused.
					SystemInstanceController->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
					SystemInstanceController->ManualTick(SeekDelta, nullptr);
				}
			}
			else if (AgeDiff < MaxForwardFrames * SeekDelta)
			{
				// Allow ticks between 0 and MaxForwardFrames, but don't ever send more then 2 x the seek delta.
				SystemInstanceController->ManualTick(FMath::Min(AgeDiff, 2 * SeekDelta), nullptr);
			}
			else
			{
				// When going forward in time for more than MaxForwardFrames, reset and simulate a single frame.
				SystemInstanceController->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
				SystemInstanceController->ManualTick(SeekDelta, nullptr);
			}
			LastHandledDesiredAge = DesiredAge;
		}

		if (SceneProxy != nullptr)
		{
			FNiagaraSceneProxy* NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy);
			NiagaraProxy->SetRenderingEnabled(bRenderingEnabled && (bCanRenderWhileSeeking || bIsSeeking == false));
		}
	}
}

const UObject* UNiagaraComponent::AdditionalStatObject() const
{
	return Asset;
}

void UNiagaraComponent::ResetSystem()
{
	Activate(true);
}

void UNiagaraComponent::ReinitializeSystem()
{
	const bool bCachedAutoDestroy = bAutoDestroy;
	bAutoDestroy = false;
	DestroyInstance();
	bAutoDestroy = bCachedAutoDestroy;

	Activate(true);
}

bool UNiagaraComponent::GetRenderingEnabled() const
{
	return bRenderingEnabled;
}

void UNiagaraComponent::SetRenderingEnabled(bool bInRenderingEnabled)
{
	bRenderingEnabled = bInRenderingEnabled;
}

void UNiagaraComponent::AdvanceSimulation(int32 TickCount, float TickDeltaSeconds)
{
	if (SystemInstanceController.IsValid() && TickDeltaSeconds > SMALL_NUMBER)
	{
		SystemInstanceController->AdvanceSimulation(TickCount, TickDeltaSeconds);
	}
}

void UNiagaraComponent::AdvanceSimulationByTime(float SimulateTime, float TickDeltaSeconds)
{
	if (SystemInstanceController.IsValid() && TickDeltaSeconds > SMALL_NUMBER)
	{
		int32 TickCount = SimulateTime / TickDeltaSeconds;
		SystemInstanceController->AdvanceSimulation(TickCount, TickDeltaSeconds);
	}
}

void UNiagaraComponent::SetPaused(bool bInPaused)
{
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetPaused(bInPaused);
	}
}

bool UNiagaraComponent::IsPaused()const
{
	if (SystemInstanceController.IsValid())
	{
		return SystemInstanceController->IsPaused();
	}
	return false;
}

UNiagaraDataInterface* UNiagaraComponent::GetDataInterface(const FString& Name)
{
	return UNiagaraFunctionLibrary::GetDataInterface(UNiagaraDataInterface::StaticClass(), this, *Name);
}

bool UNiagaraComponent::IsWorldReadyToRun() const
{
	// The niagara system instance assumes that a batcher exists when it is created. We need to wait until this has happened before successfully activating this system.
	bool FXSystemExists = false;
	bool WorldManagerExists = false;
	UWorld* World = GetWorld();
	if (World)
	{
		if (World->Scene)
		{
			FFXSystemInterface*  FXSystemInterface = World->Scene->GetFXSystem();
			if (FXSystemInterface)
			{
				NiagaraEmitterInstanceBatcher* FoundBatcher = static_cast<NiagaraEmitterInstanceBatcher*>(FXSystemInterface->GetInterface(NiagaraEmitterInstanceBatcher::Name));
				if (FoundBatcher != nullptr)
				{
					FXSystemExists = true;
				}
			}
		}

		FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
		if (WorldManager)
		{
			WorldManagerExists = true;
		}
	}

	return WorldManagerExists && FXSystemExists;
}

bool UNiagaraComponent::InitializeSystem()
{
	if (SystemInstanceController.IsValid() == false)
	{
		LLM_SCOPE(ELLMTag::Niagara);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
		
		UWorld* World = GetWorld();
		check(World);
		check(Asset);

		const bool bPooled = PoolingMethod != ENCPoolMethod::None;

		SystemInstanceController = MakeShared<FNiagaraSystemInstanceController, ESPMode::ThreadSafe>();
		SystemInstanceController->Initialize(*World, *Asset, &OverrideParameters, this, TickBehavior, bPooled, RandomSeedOffset, bForceSolo);
		SystemInstanceController->SetOnPostTick(FNiagaraSystemInstance::FOnPostTick::CreateUObject(this, &UNiagaraComponent::PostSystemTick_GameThread));
		SystemInstanceController->SetOnComplete(FNiagaraSystemInstance::FOnComplete::CreateUObject(this, &UNiagaraComponent::OnSystemComplete));

		// NOTE: For now, we're guaranteed this will be called on the game thread when setting material variables.
		SystemInstanceController->SetOnMaterialsUpdated(FNiagaraSystemInstanceController::FOnMaterialsUpdated::CreateUObject(this, &UNiagaraComponent::MarkRenderStateDirty));

		if (bEnableGpuComputeDebug)
		{
			SystemInstanceController->SetGpuComputeDebug(bEnableGpuComputeDebug != 0);
		}

#if WITH_EDITORONLY_DATA
		OnSystemInstanceChangedDelegate.Broadcast();
#endif

		MarkRenderStateDirty();

		if (GIsAutomationTesting && GetAsset())
		{
			UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent> System %s initialized."), *GetAsset()->GetPathName());
		}
		return true;
	}
	return false;
}

void UNiagaraComponent::Activate(bool bReset /* = false */)
{
	ActivateInternal(bReset, false);
}

void UNiagaraComponent::ActivateInternal(bool bReset /* = false */, bool bIsScalabilityCull)
{
	// Handle component ticking correct
	struct FScopedComponentTickEnabled
	{
		FScopedComponentTickEnabled(UNiagaraComponent* InComponent) : Component(InComponent) {}
		~FScopedComponentTickEnabled() { Component->SetComponentTickEnabled(bTickEnabled); }
		UNiagaraComponent* Component = nullptr;
		bool bTickEnabled = false;
	};
	FScopedComponentTickEnabled ScopedComponentTickEnabled(this);

	bAwaitingActivationDueToNotReady = false;

	// Reset our local bounds on reset
	if (bReset)
	{
		CurrLocalBounds.Init();
	}

	if (GbSuppressNiagaraSystems != 0)
	{
		UnregisterWithScalabilityManager();
		OnSystemComplete(true);
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentActivate);
	if (Asset == nullptr)
	{
		DestroyInstance();
		if (GNiagaraComponentWarnNullAsset && !HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject | RF_ClassDefaultObject))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to activate Niagara Component due to missing or invalid asset! (%s)"), *GetFullName());
		}
		return;
	}

	UWorld* World = GetWorld();
	// If the particle system can't ever render (ie on dedicated server or in a commandlet) than do not activate...
	if (!FApp::CanEverRender() || !World || World->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	if (!IsRegistered())
	{
		return;
	}

	// Should we force activation to fail?
	if (UNiagaraComponentSettings::ShouldSuppressActivation(Asset))
	{
		return;
	}


	// On the off chance that the user changed the asset, we need to clear out the existing data.
	if (SystemInstanceController.IsValid() && SystemInstanceController->GetSystem() != Asset)
	{
		UnregisterWithScalabilityManager();
		OnSystemComplete(true);
	}

#if WITH_EDITOR
	// In case we're not yet ready to run due to compilation requests, go ahead and keep polling there..
	if (Asset->HasOutstandingCompilationRequests(true))
	{
		if (bWaitForCompilationOnActivate || GNiagaraForceWaitForCompilationOnActivate || GIsAutomationTesting)
		{
			Asset->WaitForCompilationComplete(true);
		}
		Asset->PollForCompilationComplete();
	}
#endif
	
	if (!Asset->IsReadyToRun() || !IsWorldReadyToRun())
	{
		bAwaitingActivationDueToNotReady = true;
		bActivateShouldResetWhenReady = bReset;
		ScopedComponentTickEnabled.bTickEnabled = true;
		return;
	}

	if (bReset)
	{
		UnregisterWithScalabilityManager();
	}

	if (!bIsScalabilityCull && bIsCulledByScalability && ScalabilityManagerHandle != INDEX_NONE)
	{
		//If this is a non scalability activate call and we're still registered with the manager.
		//If we reach this point then we must have been previously culled by scalability so bail here.
		return;
	}

	bIsCulledByScalability = false; 
	if (ShouldPreCull())
	{
		//We have decided to pre cull the system.
		OnSystemComplete(true);
		return;
	}

	// Early out if we're not forcing a reset, and both the component and system instance are already active.
	if (bReset == false &&
		IsActive() &&
		SystemInstanceController != nullptr &&
		SystemInstanceController->GetRequestedExecutionState() == ENiagaraExecutionState::Active &&
		SystemInstanceController->GetActualExecutionState() == ENiagaraExecutionState::Active)
	{
		return;
	}

	// We can't call 'Super::Activate(bReset);' as this will enable the component tick
	if (bReset || ShouldActivate() == true)
	{
		SetActiveFlag(true);
		OnComponentActivated.Broadcast(this, bReset);
	}
	
	// Auto attach if requested
	const bool bWasAutoAttached = bDidAutoAttach;
	bDidAutoAttach = false;
	if (bAutoManageAttachment)
	{
		USceneComponent* NewParent = AutoAttachParent.Get();
		if (NewParent)
		{
			const bool bAlreadyAttached = GetAttachParent() && (GetAttachParent() == NewParent) && (GetAttachSocketName() == AutoAttachSocketName) && GetAttachParent()->GetAttachChildren().Contains(this);
			if (!bAlreadyAttached)
			{
				bDidAutoAttach = bWasAutoAttached;
				CancelAutoAttachment(true);
				SavedAutoAttachRelativeLocation = GetRelativeLocation();
				SavedAutoAttachRelativeRotation = GetRelativeRotation();
				SavedAutoAttachRelativeScale3D = GetRelativeScale3D();
				//bIsChangingAutoAttachment = true;
				AttachToComponent(NewParent, FAttachmentTransformRules(AutoAttachLocationRule, AutoAttachRotationRule, AutoAttachScaleRule, bAutoAttachWeldSimulatedBodies), AutoAttachSocketName);
				//bIsChangingAutoAttachment = false;
			}

			bDidAutoAttach = true;
			//bFlagAsJustAttached = true;
		}
		else
		{
			CancelAutoAttachment(true);
		}
	}
	
#if WITH_EDITOR
	//TODO: Do this else where. I get needing to ensure params are correct from the component but these are stomping over runtime changes to the params in editor builds.
	//For now we can bypass the worst of the impact by disallowing in game worlds.
	if(!World->IsGameWorld())
	{
		ApplyOverridesToParameterStore();
	}
#endif

	FNiagaraSystemInstance::EResetMode ResetMode = FNiagaraSystemInstance::EResetMode::ResetSystem;
	if (InitializeSystem())
	{
		ResetMode = FNiagaraSystemInstance::EResetMode::None;//Already done a reinit sete
	}

	if (!SystemInstanceController)
	{
		OnSystemComplete(true);
		return;
	}

	//We reset last render time to the current time so that any visibility culling on a delay will function correctly.
	//Leaving as the default of -1000 causes the visibility code to always assume this should be culled until it's first rendered and initialized by the RT.
	SetLastRenderTime(GetWorld()->GetTimeSeconds());
	SystemInstanceController->SetLastRenderTime(GetLastRenderTime());

	RegisterWithScalabilityManager();

	// NOTE: This call can cause SystemInstance itself to get destroyed with auto destroy systems
	SystemInstanceController->Activate(ResetMode);
	
	if (SystemInstanceController && SystemInstanceController->IsSolo())
	{
		const ETickingGroup SoloTickGroup = SystemInstanceController->CalculateTickGroup();
		PrimaryComponentTick.TickGroup = FMath::Max(GNiagaraSoloTickEarly ? TG_PrePhysics : TG_DuringPhysics, SoloTickGroup);
		PrimaryComponentTick.EndTickGroup = GNiagaraSoloAllowAsyncWorkToEndOfFrame ? TG_LastDemotable : ETickingGroup(PrimaryComponentTick.TickGroup);

		// Solo instances require the component tick to be enabled
		ScopedComponentTickEnabled.bTickEnabled = true;
	}
}

void UNiagaraComponent::Deactivate()
{
	DeactivateInternal(false);
}

void UNiagaraComponent::DeactivateInternal(bool bIsScalabilityCull /* = false */)
{
	bool bWasCulledByScalabiltiy = bIsCulledByScalability;

	if (bIsScalabilityCull)
	{
		bIsCulledByScalability = true;
	}
	else
	{
		// Unregister with the scalability manager if this is a genuine deactivation from outside.
		// The scalability manager itself can call this function when culling systems.
		UnregisterWithScalabilityManager();
	}

	if (IsActive() && SystemInstanceController)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentDeactivate);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);

		//UE_LOG(LogNiagara, Log, TEXT("Deactivate: %p - %s - %s"), this, *Asset->GetName(), bIsScalabilityCull ? TEXT("Scalability") : TEXT(""));

		// Don't deactivate in solo mode as we are not ticked by the world but rather the component
		// Deactivating will cause the system to never Complete
		if (SystemInstanceController->IsSolo() == false)
		{
			Super::Deactivate();
		}

		SystemInstanceController->Deactivate();

		// We are considered active until we are complete
		// Note: Deactivate call can finalize -> complete the system -> release to pool -> unregister which will result in a nullptr for the SystemInstance
		SetActiveFlag(SystemInstanceController ? !SystemInstanceController->IsComplete() : false);
	}
	else
	{
		Super::Deactivate();

		if(bWasCulledByScalabiltiy && !bIsCulledByScalability)//We were culled by scalability but no longer, ensure we've handled completion correctly. E.g. returned to the pool etc.
		{
			OnSystemComplete(true);
		}
		SetActiveFlag(false);
	}
}

void UNiagaraComponent::DeactivateImmediate()
{
	DeactivateImmediateInternal(false);
}

void UNiagaraComponent::DeactivateImmediateInternal(bool bIsScalabilityCull)
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentDeactivate);
	Super::Deactivate();

	bool bWasCulledByScalability = bIsCulledByScalability;

	//UE_LOG(LogNiagara, Log, TEXT("DeactivateImmediate: %p - %s - %s"), this, *Asset->GetName(), bIsScalabilityCull ? TEXT("Scalability") : TEXT(""));

	//UE_LOG(LogNiagara, Log, TEXT("Deactivate %s"), *GetName());

	//Unregister with the scalability manager if this is a genuine deactivation from outside.
	//The scalability manager itself can call this function when culling systems.
	if (bIsScalabilityCull)
	{
		bIsCulledByScalability = true;
	}
	else
	{
		UnregisterWithScalabilityManager();
	}

	SetActiveFlag(false);

	if (SystemInstanceController)
	{
		SystemInstanceController->Deactivate(true);
	}
	else if (bWasCulledByScalability && !bIsCulledByScalability)//We were culled by scalability but no longer, ensure we've handled completion correctly. E.g. returned to the pool etc.
	{
		OnSystemComplete(true);
	}
}

bool UNiagaraComponent::ShouldPreCull()
{
	if (bAllowScalability)
	{
		if (UNiagaraSystem* System = GetAsset())
		{
			if (UNiagaraEffectType* EffectType = System->GetEffectType())
			{
				if (FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(GetWorld()))
				{
					if (EffectType->UpdateFrequency == ENiagaraScalabilityUpdateFrequency::SpawnOnly)
					{
						//If we're just set to check on spawn then check for precull here.
						return WorldMan->ShouldPreCull(GetAsset(), this);
					}
				}
			}
		}
	}

	return false;
}

void UNiagaraComponent::RegisterWithScalabilityManager()
{
	if (ScalabilityManagerHandle == INDEX_NONE && bAllowScalability)
	{
		if (UNiagaraSystem* System = GetAsset())
		{
			if (UNiagaraEffectType* EffectType = System->GetEffectType())
			{
				if (FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(GetWorld()))
				{
					WorldMan->RegisterWithScalabilityManager(this);
				}
			}
		}
	}
}

void UNiagaraComponent::UnregisterWithScalabilityManager()
{
	if (ScalabilityManagerHandle != INDEX_NONE)
	{
		if (FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(GetWorld()))
		{
			WorldMan->UnregisterWithScalabilityManager(this);
		}
	}
	bIsCulledByScalability = false;
	ScalabilityManagerHandle = INDEX_NONE;//Just to be sure our state is unregistered.
}

void UNiagaraComponent::PostSystemTick_GameThread()
{
	check(SystemInstanceController.IsValid()); // sanity

#if WITH_EDITOR
	if (SystemInstanceController->HandleNeedsUIResync())
	{
		OnSynchronizedWithAssetParametersDelegate.Broadcast();
	}
#endif

	// NOTE: Since this is happening before scene visibility calculation, it's likely going to be off by a frame
	SystemInstanceController->SetLastRenderTime(GetLastRenderTime());

	MarkRenderDynamicDataDirty();

	// Check to force update our transform based on a timer or bounds expanding beyond their previous local boundaries
	const FBox NewLocalBounds = SystemInstanceController->GetLocalBounds();
	ForceUpdateTransformTime += GetWorld()->GetDeltaSeconds();
	if (!CurrLocalBounds.IsValid ||
		!CurrLocalBounds.IsInsideOrOn(NewLocalBounds.Min) ||
		!CurrLocalBounds.IsInsideOrOn(NewLocalBounds.Max) ||
		(ForceUpdateTransformTime > MaxTimeBeforeForceUpdateTransform))
	{
		CurrLocalBounds = NewLocalBounds;
		ForceUpdateTransformTime = 0.0f;
		UpdateComponentToWorld();
	}

	// Give renderers a chance to do some processing PostTick
	if (auto NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy))
	{
		if (auto RenderData = NiagaraProxy->GetSystemRenderData())
		{
			SystemInstanceController->PostTickRenderers(*RenderData);
		}
	}
}

void UNiagaraComponent::OnSystemComplete(bool bExternalCompletion)
{
	// Debug feature, if we have loop enabled all world FX will loop unless deactivate by scalability
#if !UE_BUILD_SHIPPING
	if ( !bExternalCompletion && !bIsCulledByScalability )
	{
		if ( UWorld* World = GetWorld() )
		{
			FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(GetWorld());
			if ( WorldManager->GetDebugPlaybackMode() == ENiagaraDebugPlaybackMode::Loop )
			{
				// If we have a loop time set the WorldManager will force a reset, so we will just ignore the completion event in that case
				static const auto CVarGlobalLoopTime = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.Niagara.Debug.GlobalLoopTime"));
				if (CVarGlobalLoopTime && (CVarGlobalLoopTime->GetFloat() <= 0.0f))
				{
					Activate(true);
				}
				return;
			}
		}
	}
#endif

	//UE_LOG(LogNiagara, Log, TEXT("OnSystemComplete: %p - %s"), SystemInstance.Get(), *Asset->GetName());
	SetComponentTickEnabled(false);
	SetActiveFlag(false);

	MarkRenderDynamicDataDirty();
	//TODO: Mark the render state dirty?

	//Don't really complete if we're being culled by scalability.
	//We want to stop ticking but not be reclaimed by the pools etc.
	//We also want to skip this work if we're destroying during and update context reset.
	if (bIsCulledByScalability == false && bDuringUpdateContextReset == false)
	{		
		//UE_LOG(LogNiagara, Log, TEXT("OnSystemFinished.Broadcast(this);: { %p -  %p - %s"), this, SystemInstance.Get(), *Asset->GetName());
		OnSystemFinished.Broadcast(this);
		//UE_LOG(LogNiagara, Log, TEXT("OnSystemFinished.Broadcast(this);: } %p - %s"), SystemInstance.Get(), *Asset->GetName());

		if (PoolingMethod == ENCPoolMethod::AutoRelease)//Don't release back to the pool if we're completing due to scalability culling.
		{
			FNiagaraWorldManager::Get(GetWorld())->GetComponentPool()->ReclaimWorldParticleSystem(this);
		}
		else if (PoolingMethod == ENCPoolMethod::ManualRelease_OnComplete)
		{
			PoolingMethod = ENCPoolMethod::ManualRelease;
			FNiagaraWorldManager::Get(GetWorld())->GetComponentPool()->ReclaimWorldParticleSystem(this);
		}
		else if (bAutoDestroy)
		{
			//UE_LOG(LogNiagara, Log, TEXT("OnSystemComplete DestroyComponent();: { %p - %s"), SystemInstance.Get(), *Asset->GetName());
			DestroyComponent();
			//UE_LOG(LogNiagara, Log, TEXT("OnSystemComplete DestroyComponent();: } %p - %s"), SystemInstance.Get(), *Asset->GetName());
		}
		else if (bAutoManageAttachment)
		{
			CancelAutoAttachment(/*bDetachFromParent=*/ true);
		}

		if (IsRegisteredWithScalabilityManager())
		{
			if (bExternalCompletion == false)
			{
				//Can we be sure this isn't going to spam erroneously?
				if (UNiagaraEffectType* EffectType = GetAsset()->GetEffectType())
				{
					//Only trigger warning if we're not being deactivated/completed from the outside and this is a natural completion by the system itself.
					if (EffectType->CullReaction == ENiagaraCullReaction::DeactivateImmediateResume || EffectType->CullReaction == ENiagaraCullReaction::DeactivateResume)
					{
						if (GNiagaraComponentWarnAsleepCullReaction == 1)
						{
							//If we're completing naturally, i.e. we're a burst/non-looping system then we shouldn't be using a mode reactivates the effect.
							UE_LOG(LogNiagara, Warning, TEXT("Niagara Effect has completed naturally but has an effect type with the \"Asleep\" cull reaction. If an effect like this is culled before it can complete then it could leak into the scalability manager and be reactivated incorrectly. Please verify this is using the correct EffectType.\nComponent:%s\nSystem:%s")
								, *GetFullName(), *GetAsset()->GetFullName());
						}
					}
				}
			}
			//We've completed naturally so unregister with the scalability manager.
			UnregisterWithScalabilityManager();
		}
	}

	// Give renderers a chance to handle completion
	if (SystemInstanceController)
	{
		if (auto NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy))
		{
			if (auto RenderData = NiagaraProxy->GetSystemRenderData())
			{
				SystemInstanceController->NotifyRenderersComplete(*RenderData);
			}
		}
	}

	if (GIsAutomationTesting && GetAsset())
	{
		UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent> System %s completed."), *GetAsset()->GetPathName());
	}
}

void UNiagaraComponent::DestroyInstance()
{
	//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::DestroyInstance: %p - %p  %s\n"), this, SystemInstance.Get(), *GetAsset()->GetFullName());
	//UE_LOG(LogNiagara, Log, TEXT("DestroyInstance: %p - %s"), this, *Asset->GetName());
	SetActiveFlag(false);
	UnregisterWithScalabilityManager();

	if (SystemInstanceController)
	{
		SystemInstanceController->Release();
		SystemInstanceController = nullptr;
	}

#if WITH_EDITORONLY_DATA
	OnSystemInstanceChangedDelegate.Broadcast();
#endif
	MarkRenderStateDirty();
}

void UNiagaraComponent::OnPooledReuse(UWorld* NewWorld)
{
	check(!IsPendingKill());
	SetUserParametersToDefaultValues();

	//Need to reset the component's visibility in case it's returned to the pool while marked invisible.
	SetVisibility(true);
	SetHiddenInGame(false);

	if (GetWorld() != NewWorld)
	{
		// Rename the NC to move it into the current PersistentLevel - it may have been spawned in one
		// level but is now needed in another level.
		// Use the REN_ForceNoResetLoaders flag to prevent the rename from potentially calling FlushAsyncLoading.
		Rename(nullptr, NewWorld, REN_ForceNoResetLoaders);
	}

	//We reset last render time to the current time so that any visibility culling on a delay will function correctly.
	//Leaving as the default of -1000 causes the visibility code to always assume this should be culled until it's first rendered and initialized by the RT.
	SetLastRenderTime(GetWorld()->GetTimeSeconds());

	if (SystemInstanceController != nullptr)
	{
		SystemInstanceController->OnPooledReuse(*NewWorld);
		SystemInstanceController->SetLastRenderTime(GetLastRenderTime());
	}
}

void UNiagaraComponent::OnRegister()
{
	if (IsActive() && SystemInstanceController.IsValid() == false)
	{
		// If we're active but don't have an active system instance clear the active flag so that the component
		// gets activated.
		SetActiveFlag(false);
	}

	if (bAutoManageAttachment && !IsActive())
	{
		// Detach from current parent, we are supposed to wait for activation.
		if (GetAttachParent())
		{
			// If no auto attach parent override, use the current parent when we activate
			if (!AutoAttachParent.IsValid())
			{
				AutoAttachParent = GetAttachParent();
			}
			// If no auto attach socket override, use current socket when we activate
			if (AutoAttachSocketName == NAME_None)
			{
				AutoAttachSocketName = GetAttachSocketName();
			}

			// Prevent attachment before Super::OnRegister() tries to attach us, since we only attach when activated.
			if (GetAttachParent()->GetAttachChildren().Contains(this))
			{
				// Only detach if we are not about to auto attach to the same target, that would be wasteful.
				if (!bAutoActivate || (AutoAttachLocationRule != EAttachmentRule::KeepRelative && AutoAttachRotationRule != EAttachmentRule::KeepRelative && AutoAttachScaleRule != EAttachmentRule::KeepRelative) || (AutoAttachSocketName != GetAttachSocketName()) || (AutoAttachParent != GetAttachParent()))
				{
					//bIsChangingAutoAttachment = true;
					DetachFromComponent(FDetachmentTransformRules(EDetachmentRule::KeepRelative, /*bCallModify=*/ false));
					//bIsChangingAutoAttachment = false;
				}
			}
			else
			{
				SetupAttachment(nullptr, NAME_None);
			}
		}

		SavedAutoAttachRelativeLocation = GetRelativeLocation();
		SavedAutoAttachRelativeRotation = GetRelativeRotation();
		SavedAutoAttachRelativeScale3D = GetRelativeScale3D();
	}

#if WITH_EDITOR
	if (Asset && !AssetExposedParametersChangedHandle.IsValid())
	{
		AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
	}
#endif

	Super::OnRegister();
}

bool UNiagaraComponent::IsReadyForOwnerToAutoDestroy() const
{
	return !IsActive();
}

void UNiagaraComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	//UE_LOG(LogNiagara, Log, TEXT("OnComponentDestroyed %p %p"), this, SystemInstanceController.Get());
	//DestroyInstance();//Can't do this here as we can call this from inside the system instance currently during completion 

	if (PoolingMethod != ENCPoolMethod::None)
	{
		if (UWorld* World = GetWorld())
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::OnComponentDestroyed: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying!"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);
			if (FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World))
			{
				if (UNiagaraComponentPool* ComponentPool = WorldManager->GetComponentPool())
				{
					ComponentPool->PooledComponentDestroyed(this);
				}
			}
		}
		else
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::OnComponentDestroyed: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying and world it nullptr!"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);
		}

		// Set pooling method to none as we are destroyed and can not go into the pool after this point
		PoolingMethod = ENCPoolMethod::None;
	}

	UnregisterWithScalabilityManager();

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UNiagaraComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
#if WITH_EDITOR
	// When component's properties are initialized on a component on an actor spawned into the world using a template, neither post load or serialize is
	// called on the component when it's properties are up to date so this delegate isn't bound correctly.  the asset property will be valid when this
	// callback is called so we bind the delegate here if it's not already bound.  This fixes an issue where components in the level don't save changes 
	// made to user parameters if the user parameter list is changed. 
	if (Asset != nullptr && AssetExposedParametersChangedHandle.IsValid() == false)
	{
		AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
	}
#endif
}

void UNiagaraComponent::OnUnregister()
{
	Super::OnUnregister();

	SetActiveFlag(false);

	UnregisterWithScalabilityManager();

	if (SystemInstanceController)
	{
		//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::OnUnregister: %p  %s\n"), SystemInstanceController.Get(), *GetAsset()->GetFullName());

		SystemInstanceController->Deactivate(true);

		if (PoolingMethod == ENCPoolMethod::None)
		{
			SystemInstanceController->Release();
			SystemInstanceController = nullptr;
#if WITH_EDITORONLY_DATA
			OnSystemInstanceChangedDelegate.Broadcast();
#endif
		}
	}
}

void UNiagaraComponent::BeginDestroy()
{
	//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::BeginDestroy(): %p - %d - %s\n"), this, ScalabilityManagerHandle, *GetAsset()->GetFullName());

	if (PoolingMethod != ENCPoolMethod::None)
	{
		if (UWorld* World = GetWorld())
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::BeginDestroy: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying!"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);

			if (FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World))
			{
				if (UNiagaraComponentPool* ComponentPool = WorldManager->GetComponentPool())
				{
					ComponentPool->PooledComponentDestroyed(this);
				}
			}
		}
		else
		{
			UE_LOG(LogNiagara, Warning, TEXT("UNiagaraComponent::BeginDestroy: Component (%p - %s) Asset (%s) is still pooled (%d) while destroying and world is nullptr!"), this, *GetFullNameSafe(this), *GetFullNameSafe(Asset), PoolingMethod);
		}

		// Set pooling method to none as we are destroyed and can not go into the pool after this point
		PoolingMethod = ENCPoolMethod::None;
	}

	//By now we will have already unregistered with the scalability manger. Either directly in OnComponentDestroyed, or via the post GC callbacks in the manager it's self in the case of someone calling MarkPendingKill() directly on a component.
	ScalabilityManagerHandle = INDEX_NONE;

	DestroyInstance();

	Super::BeginDestroy();
}

TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> UNiagaraComponent::GetSystemSimulation()
{
	if (SystemInstanceController)
	{
		return SystemInstanceController->GetSystemSimulation_Unsafe();
	}

	return nullptr;
}

void UNiagaraComponent::OnEndOfFrameUpdateDuringTick()
{
	Super::OnEndOfFrameUpdateDuringTick();
	if (SystemInstanceController)
	{
		SystemInstanceController->WaitForConcurrentTickAndFinalize();
	}
}

void UNiagaraComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);
	// The emitter instance may not tick again next frame so we send the dynamic data here so that the current state
	// renders.  This can happen when while editing, or any time the age update mode is set to desired age.
	SendRenderDynamicData_Concurrent();
}

void UNiagaraComponent::DestroyRenderState_Concurrent()
{
	if (SceneProxy)
	{
		static_cast<FNiagaraSceneProxy*>(SceneProxy)->DestroyRenderState_Concurrent();
	}

	Super::DestroyRenderState_Concurrent();
}

void UNiagaraComponent::SendRenderDynamicData_Concurrent()
{
	LLM_SCOPE(ELLMTag::Niagara);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentSendRenderData);
	PARTICLE_PERF_STAT_CYCLES_GT(FParticlePerfStatsContext(GetWorld(), GetAsset(), this), EndOfFrame);

	Super::SendRenderDynamicData_Concurrent();

	if (FNiagaraSceneProxy* NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy))
	{
		if (FNiagaraSystemRenderData* RenderData = NiagaraProxy->GetSystemRenderData())
		{
			FNiagaraSystemRenderData::FSetDynamicDataCommandList SetDataCommands;
			SystemInstanceController->GenerateSetDynamicDataCommands(SetDataCommands, *RenderData, *NiagaraProxy);
		
			float LocalPreviewLODDistance = -1.0f;
#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
			LocalPreviewLODDistance = GetPreviewLODDistance();
#endif

			ENQUEUE_RENDER_COMMAND(NiagaraSetDynamicData)(
				[NiagaraProxy, CommandsRT = MoveTemp(SetDataCommands), PerfStatCtx=GetPerfStatsContext(), LocalPreviewLODDistance](FRHICommandListImmediate& RHICmdList)
				{
					SCOPE_CYCLE_COUNTER(STAT_NiagaraSetDynamicData);
					PARTICLE_PERF_STAT_CYCLES_WITH_COUNT_RT(PerfStatCtx, RenderUpdate, 1);

					FNiagaraSystemRenderData::ExecuteDynamicDataCommands_RenderThread(CommandsRT);

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
					NiagaraProxy->PreviewLODDistance = LocalPreviewLODDistance;
#endif
				}
			);
		}
	}
}

int32 UNiagaraComponent::GetNumMaterials() const
{
	if (SystemInstanceController)
	{
		return SystemInstanceController->GetNumMaterials();
	}

	return 0;
}


FBoxSphereBounds UNiagaraComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const USceneComponent* UseAutoParent = (bAutoManageAttachment && GetAttachParent() == nullptr) ? AutoAttachParent.Get() : nullptr;
	if (UseAutoParent)
	{
		// We use auto attachment but have detached, don't use our own bogus bounds (we're off near 0,0,0), use the usual parent's bounds.
		return UseAutoParent->Bounds;
	}

	FBoxSphereBounds SystemBounds;
	if (CurrLocalBounds.IsValid)
	{
		SystemBounds = CurrLocalBounds;
		SystemBounds.BoxExtent *= BoundsScale;
		SystemBounds.SphereRadius *= BoundsScale;
	}
	else
	{
		FBox SimBounds(ForceInit);
		if (Asset && Asset->bFixedBounds)
		{
			SimBounds = Asset->GetFixedBounds();
		}
		SystemBounds = FBoxSphereBounds(SimBounds);
	}

	return SystemBounds.TransformBy(LocalToWorld);
}

FPrimitiveSceneProxy* UNiagaraComponent::CreateSceneProxy()
{
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraCreateSceneProxy);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);

	// The constructor will set up the System renderers from the component.
	FNiagaraSceneProxy* Proxy = new FNiagaraSceneProxy(this);
	return Proxy;
}

void UNiagaraComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->GetUsedMaterials(OutMaterials);
	}
}

void UNiagaraComponent::OnAttachmentChanged()
{
	// Uncertain about this. 
	// 	if (bIsActive && !bIsChangingAutoAttachment && !GetOwner()->IsPendingKillPending())
	// 	{
	// 		ResetSystem();
	// 	}

	Super::OnAttachmentChanged();

}

void UNiagaraComponent::OnChildAttached(USceneComponent* ChildComponent)
{
	Super::OnChildAttached(ChildComponent);

}

void UNiagaraComponent::OnChildDetached(USceneComponent* ChildComponent)
{
	Super::OnChildDetached(ChildComponent);

}

void UNiagaraComponent::SetTickBehavior(ENiagaraTickBehavior NewTickBehavior)
{
	TickBehavior = NewTickBehavior;
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetTickBehavior(TickBehavior);
	}
}

void UNiagaraComponent::SetRandomSeedOffset(int32 NewRandomSeedOffset)
{
	RandomSeedOffset = NewRandomSeedOffset;
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetRandomSeedOffset(RandomSeedOffset);
	}
}

void UNiagaraComponent::SetVariableLinearColor(FName InVariableName, const FLinearColor& InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetColorDef(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FLinearColor)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableLinearColor(const FString& InVariableName, const FLinearColor& InValue)
{
	SetVariableLinearColor(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableQuat(FName InVariableName, const FQuat& InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetQuatDef(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FQuat)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableQuat(const FString& InVariableName, const FQuat& InValue)
{
	SetVariableQuat(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableVec4(FName InVariableName, const FVector4& InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetVec4Def(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FVector4)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableVec4(const FString& InVariableName, const FVector4& InValue)
{
	SetVariableVec4(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableVec3(FName InVariableName, FVector InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetVec3Def(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FVector)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableVec3(const FString& InVariableName, FVector InValue)
{
	SetVariableVec3(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableVec2(FName InVariableName, FVector2D InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetVec2Def(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(FVector2D)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableVec2(const FString& InVariableName, FVector2D InValue)
{
	SetVariableVec2(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableFloat(FName InVariableName, float InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetFloatDef(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(float)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableFloat(const FString& InVariableName, float InValue)
{
	SetVariableFloat(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableInt(FName InVariableName, int32 InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetIntDef(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(InValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&InValue, sizeof(int32)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableInt(const FString& InVariableName, int32 InValue)
{
	SetVariableInt(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableBool(FName InVariableName, bool InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetBoolDef(), InVariableName);
	const FNiagaraBool BoolValue(InValue);
	if (SystemInstanceController.IsValid())
	{
		SystemInstanceController->SetVariable_Deferred(InVariableName, InValue);
	}
	else
	{
		OverrideParameters.SetParameterValue(BoolValue, VariableDesc, true);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(&BoolValue, sizeof(FNiagaraBool)));
#endif
}

void UNiagaraComponent::SetNiagaraVariableBool(const FString& InVariableName, bool InValue)
{
	SetVariableBool(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableActor(FName InVariableName, AActor* InValue)
{
	SetVariableObject(InVariableName, InValue);
}

void UNiagaraComponent::SetNiagaraVariableActor(const FString& InVariableName, AActor* InValue)
{
	SetNiagaraVariableObject(InVariableName, InValue);
}

void UNiagaraComponent::SetVariableObject(FName InVariableName, UObject* InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetUObjectDef(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		TWeakObjectPtr<UObject> ObjPtr = InValue;
		SystemInstanceController->SetVariable_Deferred(InVariableName, ObjPtr);
	}
	else
	{
		OverrideParameters.SetUObject(InValue, VariableDesc);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(InValue));
#endif
}

void UNiagaraComponent::SetNiagaraVariableObject(const FString& InVariableName, UObject* InValue)
{
	SetVariableObject(FName(*InVariableName), InValue);
}

void UNiagaraComponent::SetVariableMaterial(FName InVariableName, UMaterialInterface* InValue)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetUMaterialDef(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		TWeakObjectPtr<UMaterialInterface> ObjPtr = InValue;
		SystemInstanceController->SetVariable_Deferred(InVariableName, ObjPtr);
	}
	else
	{
		OverrideParameters.SetUObject(InValue, VariableDesc);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(InValue));
#endif
}

void UNiagaraComponent::SetVariableStaticMesh(FName InVariableName, UStaticMesh* InValue)
{
	if (SystemInstanceController.IsValid())
	{
		TWeakObjectPtr<UStaticMesh> ObjPtr = InValue;
		SystemInstanceController->SetVariable_Deferred(InVariableName, ObjPtr);
	}
#if WITH_EDITOR
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetUStaticMeshDef(), InVariableName);
	SetParameterOverride(VariableDesc, FNiagaraVariant(InValue));
#endif
}

void UNiagaraComponent::SetVariableTexture(FName InVariableName, UTexture* Texture)
{
	if (SystemInstanceController.IsValid())
	{
		TWeakObjectPtr<UTexture> ObjPtr = Texture;
		SystemInstanceController->SetVariable_Deferred(InVariableName, ObjPtr);
	}
#if WITH_EDITOR
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetUTextureDef(), InVariableName);
	SetParameterOverride(VariableDesc, FNiagaraVariant(Texture));
#endif
}

void UNiagaraComponent::SetVariableTextureRenderTarget(FName InVariableName, UTextureRenderTarget* TextureRenderTarget)
{
	const FNiagaraVariable VariableDesc(FNiagaraTypeDefinition::GetUTextureRenderTargetDef(), InVariableName);
	if (SystemInstanceController.IsValid())
	{
		TWeakObjectPtr<UTextureRenderTarget> ObjPtr = TextureRenderTarget;
		SystemInstanceController->SetVariable_Deferred(InVariableName, ObjPtr);
	}
	else
	{
		OverrideParameters.SetUObject(TextureRenderTarget, VariableDesc);
	}
#if WITH_EDITOR
	SetParameterOverride(VariableDesc, FNiagaraVariant(TextureRenderTarget));
#endif
}

TArray<FVector> UNiagaraComponent::GetNiagaraParticlePositions_DebugOnly(const FString& InEmitterName)
{
	return GetNiagaraParticleValueVec3_DebugOnly(InEmitterName, TEXT("Position"));
}

TArray<FVector> UNiagaraComponent::GetNiagaraParticleValueVec3_DebugOnly(const FString& InEmitterName, const FString& InValueName)
{
	TArray<FVector> Values;
	FName EmitterName = FName(*InEmitterName);
	if (SystemInstanceController.IsValid())
	{
		if (!SystemInstanceController->GetParticleValueVec3_DebugOnly(Values, EmitterName, FName(*InValueName)))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Unable to find variable %s on %s per-particle data. Returning zeroes."), *InValueName, *GetPathName());
		}
	}
	return Values;

}

TArray<float> UNiagaraComponent::GetNiagaraParticleValues_DebugOnly(const FString& InEmitterName, const FString& InValueName)
{
	TArray<float> Values;
	FName EmitterName = FName(*InEmitterName);
	if (SystemInstanceController.IsValid())
	{
		if (!SystemInstanceController->GetParticleValues_DebugOnly(Values, EmitterName, FName(*InValueName)))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Unable to find variable %s on %s per-particle data. Returning zeroes."), *InValueName, *GetPathName());
		}
	}
	return Values;
}

void FixInvalidUserParameters(FNiagaraUserRedirectionParameterStore& ParameterStore)
{
	static const FString UserPrefix = FNiagaraConstants::UserNamespace.ToString() + TEXT(".");

	TArray<FNiagaraVariable> Parameters;
	ParameterStore.GetParameters(Parameters);
	TArray<FNiagaraVariable> IncorrectlyNamedParameters;
	for (FNiagaraVariable& Parameter : Parameters)
	{
		if (Parameter.GetName().ToString().StartsWith(UserPrefix) == false)
		{
			IncorrectlyNamedParameters.Add(Parameter);
		}
	}

	bool bParameterRenamed = false;
	for(FNiagaraVariable& IncorrectlyNamedParameter : IncorrectlyNamedParameters)
	{
		FString FixedNameString = UserPrefix + IncorrectlyNamedParameter.GetName().ToString();
		FName FixedName = *FixedNameString;
		FNiagaraVariable FixedParameter(IncorrectlyNamedParameter.GetType(), FixedName);
		if (Parameters.Contains(FixedParameter))
		{
			// If the correctly named parameter is also in the collection then both parameters need to be removed and the 
			// correct one re-added.  First we need to cache the value of the parameter so that it's not lost on removal.
			UNiagaraDataInterface* DataInterfaceValue = nullptr;
			UObject* ObjectValue = nullptr;
			TArray<uint8> DataValue;
			int32 ValueIndex = ParameterStore.IndexOf(IncorrectlyNamedParameter);
			if (IncorrectlyNamedParameter.IsDataInterface())
			{
				DataInterfaceValue = ParameterStore.GetDataInterface(IncorrectlyNamedParameter);
			}
			else if (IncorrectlyNamedParameter.IsUObject())
			{
				ObjectValue = ParameterStore.GetUObject(IncorrectlyNamedParameter);
			}
			else
			{
				const uint8* DataValuePtr = ParameterStore.GetParameterData(IncorrectlyNamedParameter);
				if (DataValuePtr != nullptr)
				{
					DataValue.AddUninitialized(IncorrectlyNamedParameter.GetSizeInBytes());
					FMemory::Memcpy(DataValue.GetData(), DataValuePtr, IncorrectlyNamedParameter.GetSizeInBytes());
				}
			}

			// Next we remove the parameter twice because the first removal of the incorrect parameter will actually remove
			// the correct version due to the user redirection table.
			ParameterStore.RemoveParameter(IncorrectlyNamedParameter);
			ParameterStore.RemoveParameter(IncorrectlyNamedParameter);

			// Last we add back the fixed parameter and set the value.
			ParameterStore.AddParameter(FixedParameter, false);
			if (DataInterfaceValue != nullptr)
			{
				ParameterStore.SetDataInterface(DataInterfaceValue, FixedParameter);
			}
			else if (ObjectValue != nullptr)
			{
				ParameterStore.SetUObject(ObjectValue, FixedParameter);
			}
			else
			{
				if (DataValue.Num() == FixedParameter.GetSizeInBytes())
				{
					ParameterStore.SetParameterData(DataValue.GetData(), FixedParameter);
				}
			}
		}
		else 
		{
			// If the correctly named parameter was not in the collection already we can just rename the incorrect parameter
			// to the correct one.
			ParameterStore.RenameParameter(IncorrectlyNamedParameter, FixedName);
			bParameterRenamed = true;
		}
	}

	if (bParameterRenamed)
	{
		ParameterStore.RecreateRedirections();
	}
}

void UNiagaraComponent::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
#if WITH_EDITOR
	if (Record.GetUnderlyingArchive().IsLoading())
	{
		FixDataInterfaceOuters();

		// Check the the event handler here as well as in post load since post load isn't always called due to how components are managed when spawning
		// actors with templates.
		if (Asset != nullptr && AssetExposedParametersChangedHandle.IsValid() == false)
		{
			AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
		}
	}
#endif
}

void UNiagaraComponent::PostLoad()
{
	Super::PostLoad();

	OverrideParameters.PostLoad();
	OverrideParameters.SanityCheckData();

#if WITH_EDITOR
	if (Asset != nullptr)
	{
		Asset->ConditionalPostLoad();

		FixInvalidUserParameters(OverrideParameters);
		
		UpgradeDeprecatedParameterOverrides();

#if WITH_EDITORONLY_DATA
		const int32 NiagaraVer = GetLinkerCustomVersion(FNiagaraCustomVersion::GUID);
		if (NiagaraVer < FNiagaraCustomVersion::ComponentsOnlyHaveUserVariables)
		{
			{
				TArray<FNiagaraVariableBase> ToRemoveNonUser;
				TArray<FNiagaraVariableBase> ToAddNonUser;

				auto InstanceKeyIter = InstanceParameterOverrides.CreateConstIterator();
				while (InstanceKeyIter)
				{
					FName KeyName = InstanceKeyIter.Key().GetName();
					FNiagaraVariableBase VarBase = InstanceKeyIter.Key();
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(VarBase);
					FName UserKeyName = VarBase.GetName();
					if (KeyName != UserKeyName)
					{
						UE_LOG(LogNiagara, Log, TEXT("InstanceParameterOverrides for %s has non-user keys in it! %s. Updating in PostLoad to User key."), *GetPathName(), *KeyName.ToString());
						const FNiagaraVariant* FoundVar = InstanceParameterOverrides.Find(VarBase);
						if (FoundVar != nullptr)
						{
							UE_LOG(LogNiagara, Warning, TEXT("InstanceParameterOverrides for %s has values for both keys in it! %s and %s. PostLoad keeping User version."), *GetPathName(), *KeyName.ToString(), *UserKeyName.ToString());
						}
						else
						{
							ToAddNonUser.Add(InstanceKeyIter.Key());
						}
						ToRemoveNonUser.Add(InstanceKeyIter.Key());
					}
					++InstanceKeyIter;
				}

				for (const FNiagaraVariableBase& Var : ToAddNonUser)
				{
					const FNiagaraVariant FoundVar = InstanceParameterOverrides.FindRef(Var);
					FNiagaraVariableBase UserVar = Var;
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserVar);
					InstanceParameterOverrides.Emplace(UserVar, FoundVar);
				}

				for (const FNiagaraVariableBase& Var : ToRemoveNonUser)
				{
					InstanceParameterOverrides.Remove(Var);
				}
			}

			{
				TArray<FNiagaraVariableBase> ToRemoveNonUser;
				TArray<FNiagaraVariableBase> ToAddNonUser;

				auto TemplateKeyIter = TemplateParameterOverrides.CreateConstIterator();
				while (TemplateKeyIter)
				{
					FName KeyName = TemplateKeyIter.Key().GetName();
					FNiagaraVariableBase VarBase = TemplateKeyIter.Key();
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(VarBase);
					FName UserKeyName = VarBase.GetName();
					if (KeyName != UserKeyName)
					{
						UE_LOG(LogNiagara, Log, TEXT("TemplateParameterOverrides for %s has non-user keys in it! %s. Updating in PostLoad to User key."), *GetPathName(), *KeyName.ToString());
						const FNiagaraVariant* FoundVar = TemplateParameterOverrides.Find(VarBase);
						if (FoundVar != nullptr)
						{
							UE_LOG(LogNiagara, Warning, TEXT("TemplateParameterOverrides for %s has values for both keys in it! %s and %s.  PostLoad keeping User version."), *GetPathName(), *KeyName.ToString(), *UserKeyName.ToString());
						}
						else
						{
							ToAddNonUser.Add(TemplateKeyIter.Key());
						}
						ToRemoveNonUser.Add(TemplateKeyIter.Key());
					}
					++TemplateKeyIter;
				}

				for (const FNiagaraVariableBase& Var : ToAddNonUser)
				{
					const FNiagaraVariant FoundVar = TemplateParameterOverrides.FindRef(Var);
					FNiagaraVariableBase UserVar = Var;
					FNiagaraUserRedirectionParameterStore::MakeUserVariable(UserVar);
					TemplateParameterOverrides.Emplace(UserVar, FoundVar);
				}

				for (const FNiagaraVariableBase& Var : ToRemoveNonUser)
				{
					TemplateParameterOverrides.Remove(Var);
				}
			}
		}
#endif

		FixInvalidUserParameterOverrideData();
		SynchronizeWithSourceSystem();
		FixDataInterfaceOuters();
		OverrideParameters.SanityCheckData();

		if(AssetExposedParametersChangedHandle.IsValid() == false)
		{
			AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
		}
	}
#endif
}

static FNiagaraVariant GetParameterValueFromStore(const FNiagaraVariableBase& Var, const FNiagaraParameterStore& Store)
{
	if (Var.IsDataInterface())
	{
		const int32 Index = Store.IndexOf(Var);
		if (Index != INDEX_NONE)
		{
			return FNiagaraVariant(Store.GetDataInterfaces()[Index]);
		}
	}
	else if (Var.IsUObject())
	{
		const int32 Index = Store.IndexOf(Var);
		if (Index != INDEX_NONE)
		{
			return FNiagaraVariant(Store.GetUObjects()[Index]);
		}
	}

	const uint8* ParameterData = Store.GetParameterData(Var);
	if (ParameterData == nullptr)
	{
		return FNiagaraVariant();
	}

	return FNiagaraVariant(ParameterData, Var.GetSizeInBytes());
}

#if WITH_EDITOR

void UNiagaraComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange != nullptr && 
		PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, Asset) && 
		Asset != nullptr)
	{
		Asset->GetExposedParameters().RemoveOnChangedHandler(AssetExposedParametersChangedHandle);
		AssetExposedParametersChangedHandle.Reset();
		DestroyInstance();
	}
}

void UNiagaraComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FName PropertyName;
	if (PropertyChangedEvent.Property)
	{
		PropertyName = PropertyChangedEvent.Property->GetFName();
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, Asset))
	{
		SynchronizeWithSourceSystem();
		if (Asset != nullptr)
		{
			AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
		}
	}
	else if(PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, OverrideParameters))
	{
		SynchronizeWithSourceSystem();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, TemplateParameterOverrides) ||
		PropertyName == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, InstanceParameterOverrides))
	{
		ApplyOverridesToParameterStore();
	}

	ReinitializeSystem();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif

void UNiagaraComponent::SetUserParametersToDefaultValues()
{
#if WITH_EDITORONLY_DATA
	EditorOverridesValue_DEPRECATED.Empty();
	TemplateParameterOverrides.Empty();
	InstanceParameterOverrides.Empty();
#endif
	
	if (Asset == nullptr)
	{
		OverrideParameters.Empty(false);
		return;
	}

	if (GNiagaraUseFastSetUserParametersToDefaultValues)
	{
		const FNiagaraUserRedirectionParameterStore& SourceUserParameterStore = Asset->GetExposedParameters();
		TArrayView<const FNiagaraVariableWithOffset> DestParameters = OverrideParameters.ReadParameterVariables();

		TArray<FNiagaraVariableBase, TInlineAllocator<8>> ParametersToRemove;
		bool bInterfacesChanged = false;

		for (int32 i=0; i < DestParameters.Num(); ++i)
		{
			const FNiagaraVariableWithOffset& DestParameter = DestParameters[i];
			const int32 DestIndex = DestParameter.Offset;
			const int32 SourceIndex = SourceUserParameterStore.IndexOf(DestParameter);
			if (SourceIndex != INDEX_NONE)
			{
				if (DestParameter.IsDataInterface())
				{
					check(OverrideParameters.GetDataInterface(DestIndex) != nullptr);
					SourceUserParameterStore.GetDataInterface(SourceIndex)->CopyTo(OverrideParameters.GetDataInterface(DestIndex));
					bInterfacesChanged = true;
				}
				else if (DestParameter.IsUObject())
				{
					OverrideParameters.SetUObject(SourceUserParameterStore.GetUObject(SourceIndex), DestIndex);
				}
				else
				{
					OverrideParameters.SetParameterData(SourceUserParameterStore.GetParameterData(SourceIndex), DestIndex, DestParameter.GetSizeInBytes());
				}
			}
			else
			{
				ParametersToRemove.Add(DestParameter);
			}
		}

		for (const FNiagaraVariableBase& ParameterToRemove : ParametersToRemove)
		{
			OverrideParameters.RemoveParameter(ParameterToRemove);
		}

		if (bInterfacesChanged)
		{
			OverrideParameters.OnInterfaceChange();
		}
	}
	else
	{
		OverrideParameters.Empty(false);
		CopyParametersFromAsset();
	}

	OverrideParameters.Rebind();
}

#if WITH_EDITOR

void UNiagaraComponent::UpgradeDeprecatedParameterOverrides()
{
	if (EditorOverridesValue_DEPRECATED.Num() == 0)
	{
		return;
	}

	OverrideParameters.SanityCheckData();
	PostLoadNormalizeOverrideNames();
	
	TArray<FNiagaraVariable> UserParameters;
	OverrideParameters.GetUserParameters(UserParameters);

	for (const TPair<FName, bool>& Pair : EditorOverridesValue_DEPRECATED)
	{
		FNiagaraVariable* Found = UserParameters.FindByPredicate([Pair](const FNiagaraVariable& Var)
		{
			return Var.GetName() == Pair.Key;
		});

		if (Found != nullptr)
		{
			FNiagaraVariant StoredValue = GetParameterValueFromStore(*Found, OverrideParameters);

			if (StoredValue.IsValid())
			{
				SetParameterOverride(*Found, StoredValue);
			}
		}
	}

	EditorOverridesValue_DEPRECATED.Empty();
}

void UNiagaraComponent::EnsureOverrideParametersConsistent() const
{
	if (Asset == nullptr)
	{
		return;
	}
	
	TArray<FNiagaraVariable> UserParameters;
	Asset->GetExposedParameters().GetUserParameters(UserParameters);
	
	for (const FNiagaraVariable& Key : UserParameters)
	{
		FNiagaraVariant OverrideValue = FindParameterOverride(Key);
		if (OverrideValue.IsValid())
		{
			if (Key.IsDataInterface())
			{
				const UNiagaraDataInterface* ActualDI = OverrideParameters.GetDataInterface(Key);
				if (ActualDI != nullptr)
				{
					ensureAlways(OverrideValue.GetDataInterface()->Equals(ActualDI));
				}
			}
			else if (Key.IsUObject())
			{
				const UObject* ActualObj = OverrideParameters.GetUObject(Key);
				if (ActualObj != nullptr)
				{
					ensureAlways(OverrideValue.GetUObject() == ActualObj);
				}
			}
			else
			{
				const uint8* ActualData = OverrideParameters.GetParameterData(Key);
				if (ActualData != nullptr)
				{
					ensureAlways(FMemory::Memcmp(ActualData, OverrideValue.GetBytes(), Key.GetSizeInBytes()) == 0);
				}
			}
		}
	}
}

void UNiagaraComponent::ApplyOverridesToParameterStore()
{
	if (!IsTemplate())
	{
		if (UNiagaraComponent* Archetype = Cast<UNiagaraComponent>(GetArchetype()))
		{
			TemplateParameterOverrides = Archetype->TemplateParameterOverrides;
		}
	}

	for (const auto& Pair : TemplateParameterOverrides)
	{ 
		if (!FNiagaraUserRedirectionParameterStore::IsUserParameter(Pair.Key))
		{
			continue;
		}

		const int32* ExistingParam = OverrideParameters.FindParameterOffset(Pair.Key);
		if (ExistingParam != nullptr)
		{
			SetOverrideParameterStoreValue(Pair.Key, Pair.Value);
		}
	}

	if (!IsTemplate())
	{
		for (const auto& Pair : InstanceParameterOverrides)
		{ 
			if (!FNiagaraUserRedirectionParameterStore::IsUserParameter(Pair.Key))
			{
				continue;
			}

			const int32* ExistingParam = OverrideParameters.FindParameterOffset(Pair.Key);
			if (ExistingParam != nullptr)
			{
				SetOverrideParameterStoreValue(Pair.Key, Pair.Value);
			}
		}
	}

	EnsureOverrideParametersConsistent();
}

void UNiagaraComponent::FixDataInterfaceOuters()
{
	auto FixParameterOverrides = [this](TMap<FNiagaraVariableBase, FNiagaraVariant>& ParameterOverrides, TMap<FNiagaraVariableBase, UNiagaraDataInterface*>& OutFixedDataInterfaces)
	{
		for (TPair<FNiagaraVariableBase, FNiagaraVariant>& VariableValuePair : ParameterOverrides)
		{
			if (VariableValuePair.Key.IsDataInterface() && VariableValuePair.Value.GetDataInterface() != nullptr)
			{
				if (VariableValuePair.Value.GetDataInterface()->GetOuter() != this)
				{
					UNiagaraDataInterface* FixedDataInterface = NewObject<UNiagaraDataInterface>(this, VariableValuePair.Value.GetDataInterface()->GetClass(), NAME_None, RF_Transactional | RF_Public);
					VariableValuePair.Value.GetDataInterface()->CopyTo(FixedDataInterface);
					VariableValuePair.Value.SetDataInterface(FixedDataInterface);
					OutFixedDataInterfaces.Add(VariableValuePair.Key, FixedDataInterface);
				}
			}
		}
	};

	// Fix data interfaces in the template and instance overrides and collect the replacements.
	TMap<FNiagaraVariableBase, UNiagaraDataInterface*> VariableToFixedDataInterfaces;
	FixParameterOverrides(TemplateParameterOverrides, VariableToFixedDataInterfaces);
	FixParameterOverrides(InstanceParameterOverrides, VariableToFixedDataInterfaces);

	// Update data interfaces in use in the override parameters using the replacement map.
	for (TPair<FNiagaraVariableBase, UNiagaraDataInterface*>& VariableFixedDataInterfacePair : VariableToFixedDataInterfaces)
	{
		int32 OverrideParametersIndex = OverrideParameters.IndexOf(VariableFixedDataInterfacePair.Key);
		if (OverrideParametersIndex != INDEX_NONE)
		{
			OverrideParameters.SetDataInterface(VariableFixedDataInterfacePair.Value, OverrideParametersIndex);
		}
	}

	// Fix any additional data interfaces in the override parameters which are still incorrect.
	for (int32 i = 0; i < OverrideParameters.Num(); i++)
	{
		UNiagaraDataInterface* OverrideParameterDataInterface = OverrideParameters.GetDataInterface(i);
		if (OverrideParameterDataInterface != nullptr && OverrideParameterDataInterface->GetOuter() != this)
		{
			UNiagaraDataInterface* FixedDataInterface = NewObject<UNiagaraDataInterface>(this, OverrideParameterDataInterface->GetClass(), NAME_None, RF_Transactional | RF_Public);
			OverrideParameterDataInterface->CopyTo(FixedDataInterface);
			OverrideParameters.SetDataInterface(FixedDataInterface, i);
		}
	}
}

#endif

void UNiagaraComponent::CopyParametersFromAsset(bool bResetExistingOverrideParameters)
{
	TArray<FNiagaraVariable> SourceVars;
	Asset->GetExposedParameters().GetParameters(SourceVars);
	for (FNiagaraVariable& Param : SourceVars)
	{
		OverrideParameters.AddParameter(Param, true);
	}

	TArray<FNiagaraVariable> ExistingVars;
	OverrideParameters.GetParameters(ExistingVars);

	for (FNiagaraVariable ExistingVar : ExistingVars)
	{
		if (SourceVars.Contains(ExistingVar))
		{
			if (bResetExistingOverrideParameters)
			{
				Asset->GetExposedParameters().CopyParameterData(OverrideParameters, ExistingVar);
			}
		}
		else
		{
			OverrideParameters.RemoveParameter(ExistingVar);
		}
	}
}

void UNiagaraComponent::SynchronizeWithSourceSystem()
{
	// Synchronizing parameters will create new data interface objects and if the old data
	// interface objects are currently being used by a simulation they may be destroyed due to garbage
	// collection, so preemptively kill the instance here.
	DestroyInstance();

	//TODO: Look through params in system in "Owner" namespace and add to our parameters.
	if (Asset == nullptr)
	{
#if WITH_EDITORONLY_DATA
		OverrideParameters.Empty();
		EditorOverridesValue_DEPRECATED.Empty();
		
		OnSynchronizedWithAssetParametersDelegate.Broadcast();
#endif
		return;
	}

#if WITH_EDITOR
	CopyParametersFromAsset();
	ApplyOverridesToParameterStore();
#endif

	OverrideParameters.Rebind();

#if WITH_EDITORONLY_DATA
	OnSynchronizedWithAssetParametersDelegate.Broadcast();
#endif
}

#if WITH_EDITORONLY_DATA
void FixInvalidDataInterfaceOverrides(TMap<FNiagaraVariableBase, FNiagaraVariant>& ParameterOverrides, const FString& OverrideSource, UNiagaraComponent* OwningComponent)
{
	for (TPair<FNiagaraVariableBase, FNiagaraVariant>& VariableValuePair : ParameterOverrides)
	{
		FNiagaraVariableBase& Variable = VariableValuePair.Key;
		FNiagaraVariant& Value = VariableValuePair.Value;
		if (Variable.IsDataInterface())
		{
			if (Value.GetDataInterface() == nullptr)
			{
				UE_LOG(LogNiagara, Warning, TEXT("Replaced invalid user parameter data interface with it's default.  Component: %s Override Source: %s Parameter Name: %s."), *OwningComponent->GetPathName(), *OverrideSource, *Variable.GetName().ToString());
				Value.SetDataInterface(NewObject<UNiagaraDataInterface>(OwningComponent, Variable.GetType().GetClass(), NAME_None, RF_Transactional | RF_Public));
			}
		}
	}
}

void UNiagaraComponent::FixInvalidUserParameterOverrideData()
{
	FixInvalidDataInterfaceOverrides(TemplateParameterOverrides, TEXT("Template"), this);
	FixInvalidDataInterfaceOverrides(InstanceParameterOverrides, TEXT("Instance"), this);
}
#endif

void UNiagaraComponent::AssetExposedParametersChanged()
{
	// don't worry about doing anything based on the change broadcast if we haven't finished loading
	if (!HasAnyFlags(RF_NeedPostLoad))
	{
		SynchronizeWithSourceSystem();
		ReinitializeSystem();
	}
}

#if WITH_EDITOR

bool UNiagaraComponent::HasParameterOverride(const FNiagaraVariableBase& InKey) const 
{
	FNiagaraVariableBase UserVariable = InKey;

	if (Asset)
	{
		if (!Asset->GetExposedParameters().RedirectUserVariable(UserVariable))
		{
			return false;
		}
	}
	else if (!FNiagaraUserRedirectionParameterStore::IsUserParameter(UserVariable))
	{
		return false;
	}

	if (IsTemplate())
	{
		const FNiagaraVariant* ThisValue = TemplateParameterOverrides.Find(UserVariable);

		const FNiagaraVariant* ArchetypeValue = nullptr;
		if (const UNiagaraComponent* Archetype = Cast<UNiagaraComponent>(GetArchetype()))
		{
			ArchetypeValue = Archetype->TemplateParameterOverrides.Find(UserVariable);
		}

		if (ThisValue != nullptr && ArchetypeValue != nullptr)
		{
			// exists in both, check values
			return *ThisValue != *ArchetypeValue;
		}
		else if (ThisValue != nullptr || ArchetypeValue != nullptr)
		{
			// either added or removed in this
			return true;
		}
	}
	else
	{
		if (InstanceParameterOverrides.Contains(UserVariable))
		{
			return true;
		}
	}

	return false;
}

FNiagaraVariant UNiagaraComponent::FindParameterOverride(const FNiagaraVariableBase& InKey) const
{
	if (Asset == nullptr)
	{
		return FNiagaraVariant();
	}

	FNiagaraVariableBase UserVariable = InKey;

	const FNiagaraUserRedirectionParameterStore& ParameterStore = Asset->GetExposedParameters();

	if (!ParameterStore.RedirectUserVariable(UserVariable))
	{
		return FNiagaraVariant();
	}

	if (ParameterStore.FindParameterOffset(UserVariable) == nullptr)
	{
		return FNiagaraVariant();
	}

	if (!IsTemplate())
	{
		const FNiagaraVariant* Value = InstanceParameterOverrides.Find(UserVariable);
		if (Value != nullptr)
		{
			return *Value;
		}
	}

	{
		const FNiagaraVariant* Value = TemplateParameterOverrides.Find(UserVariable);
		if (Value != nullptr)
		{
			return *Value;
		}
	}

	return FNiagaraVariant();
}

void UNiagaraComponent::SetOverrideParameterStoreValue(const FNiagaraVariableBase& InKey, const FNiagaraVariant& InValue)
{
	if (InKey.IsDataInterface())
	{
		UNiagaraDataInterface* OriginalDI = InValue.GetDataInterface();
		UNiagaraDataInterface* DuplicatedDI = DuplicateObject(OriginalDI, this);
		OverrideParameters.SetDataInterface(DuplicatedDI, InKey);
		DuplicatedDI->SetUsedByGPUEmitter(OriginalDI->IsUsedWithGPUEmitter(nullptr));
	}
	else if (InKey.IsUObject())
	{
		OverrideParameters.SetUObject(InValue.GetUObject(), InKey);
	}
	else
	{
		OverrideParameters.SetParameterData(InValue.GetBytes(), InKey, true);
	}
}

void UNiagaraComponent::SetParameterOverride(const FNiagaraVariableBase& InKey, const FNiagaraVariant& InValue)
{
	if (!ensure(InValue.IsValid()))
	{
		return;
	}

	// we want to be sure we're storing data based on the fully qualified key name (i.e. taking the user redirection into account)
	FNiagaraVariableBase UserVariable = InKey;
	if (!OverrideParameters.RedirectUserVariable(UserVariable))
	{
		return;
	}

	if (IsTemplate())
	{
		TemplateParameterOverrides.Add(UserVariable, InValue);
	}
	else
	{
		InstanceParameterOverrides.Add(UserVariable, InValue);
	}

	SetOverrideParameterStoreValue(UserVariable, InValue);
}

void UNiagaraComponent::RemoveParameterOverride(const FNiagaraVariableBase& InKey)
{
	// we want to be sure we're storing data based on the fully qualified key name (i.e. taking the user redirection into account)
	FNiagaraVariableBase UserVariable = InKey;
	if (!OverrideParameters.RedirectUserVariable(UserVariable))
	{
		return;
	}

	if (!IsTemplate())
	{
		InstanceParameterOverrides.Remove(UserVariable);
	}
	else
	{
		TemplateParameterOverrides.Remove(UserVariable);

		// we are an archetype, but check if we have an archetype and inherit the value from there
		const UNiagaraComponent* Archetype = Cast<UNiagaraComponent>(GetArchetype());
		if (Archetype != nullptr)
		{
			FNiagaraVariant ArchetypeValue = Archetype->FindParameterOverride(UserVariable);
			if (ArchetypeValue.IsValid())
			{
				// defined in archetype, reset value to that
				if (UserVariable.IsDataInterface())
				{
					UNiagaraDataInterface* DataInterface = DuplicateObject(ArchetypeValue.GetDataInterface(), this);
					TemplateParameterOverrides.Add(UserVariable, FNiagaraVariant(DataInterface));
				}
				else
				{
					TemplateParameterOverrides.Add(UserVariable, ArchetypeValue);
				}
			}
		}
	}

	SynchronizeWithSourceSystem();
}

#endif

ENiagaraAgeUpdateMode UNiagaraComponent::GetAgeUpdateMode() const
{
	return AgeUpdateMode;
}

void UNiagaraComponent::SetAgeUpdateMode(ENiagaraAgeUpdateMode InAgeUpdateMode)
{
	AgeUpdateMode = InAgeUpdateMode;
}

float UNiagaraComponent::GetDesiredAge() const
{
	return DesiredAge;
}

void UNiagaraComponent::SetDesiredAge(float InDesiredAge)
{
	DesiredAge = InDesiredAge;
	bIsSeeking = false;
}

void UNiagaraComponent::SeekToDesiredAge(float InDesiredAge)
{
	DesiredAge = InDesiredAge;
	bIsSeeking = true;
}

void UNiagaraComponent::SetCanRenderWhileSeeking(bool bInCanRenderWhileSeeking)
{
	bCanRenderWhileSeeking = bInCanRenderWhileSeeking;
}

float UNiagaraComponent::GetSeekDelta() const
{
	return SeekDelta;
}

void UNiagaraComponent::SetSeekDelta(float InSeekDelta)
{
	SeekDelta = InSeekDelta;
}

bool UNiagaraComponent::GetLockDesiredAgeDeltaTimeToSeekDelta() const
{
	return bLockDesiredAgeDeltaTimeToSeekDelta;
}

void UNiagaraComponent::SetLockDesiredAgeDeltaTimeToSeekDelta(bool bLock)
{
	bLockDesiredAgeDeltaTimeToSeekDelta = bLock;
}

float UNiagaraComponent::GetMaxSimTime() const
{
	return MaxSimTime;
}

void UNiagaraComponent::SetMaxSimTime(float InMaxTime)
{
	MaxSimTime = InMaxTime;
}

void UNiagaraComponent::SetAutoDestroy(bool bInAutoDestroy)
{
	if (ensureMsgf(!bInAutoDestroy || (PoolingMethod == ENCPoolMethod::None), TEXT("Attempting to set AutoDestroy on a pooled component!  Component(%s) Asset(%s)"), *GetFullName(), GetAsset() != nullptr ? *GetAsset()->GetPathName() : TEXT("None")))
	{
		bAutoDestroy = bInAutoDestroy;
	}
}

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
void UNiagaraComponent::SetPreviewLODDistance(bool bInEnablePreviewLODDistance, float InPreviewLODDistance)
{
	bEnablePreviewLODDistance = bInEnablePreviewLODDistance;
	PreviewLODDistance = InPreviewLODDistance;
}
#else
void UNiagaraComponent::SetPreviewLODDistance(bool bInEnablePreviewLODDistance, float InPreviewLODDistance){}
#endif

void UNiagaraComponent::SetAllowScalability(bool bAllow)
{
	bAllowScalability = bAllow; 
	if (!bAllow)
	{
		UnregisterWithScalabilityManager();
	}
}

#if WITH_EDITOR

void UNiagaraComponent::PostLoadNormalizeOverrideNames()
{
	TMap<FName, bool> ValueMap;
	for (TPair<FName, bool> Pair : EditorOverridesValue_DEPRECATED)
	{
		FString ValueNameString = Pair.Key.ToString();
		if (ValueNameString.StartsWith(TEXT("User.")))
		{
			 ValueNameString = ValueNameString.RightChop(5);
		}

		ValueMap.Add(FName(*ValueNameString), Pair.Value);
	}
	EditorOverridesValue_DEPRECATED = ValueMap;
}

#endif // WITH_EDITOR

void UNiagaraComponent::SetAsset(UNiagaraSystem* InAsset, bool bResetExistingOverrideParameters)
{
	if (Asset == InAsset)
	{
		return;
	}

	if (FNiagaraUtilities::LogVerboseWarnings())
	{
		if ( PoolingMethod != ENCPoolMethod::None )
		{
			UE_LOG(LogNiagara, Warning, TEXT("SetAsset called on pooled component '%s' Before '%s' New '%s', pleased fix calling code to not do this."), *GetFullNameSafe(this), *GetFullNameSafe(Asset), * GetFullNameSafe(InAsset));
		}
	}

#if WITH_EDITOR
	if (Asset != nullptr && AssetExposedParametersChangedHandle.IsValid())
	{
		Asset->GetExposedParameters().RemoveOnChangedHandler(AssetExposedParametersChangedHandle);
		AssetExposedParametersChangedHandle.Reset();
	}
#endif

	UnregisterWithScalabilityManager();

	const bool bWasActive = SystemInstanceController && SystemInstanceController->GetRequestedExecutionState() == ENiagaraExecutionState::Active;

	DestroyInstance();

	// Set new asset, update parameters and reactivate it it was already active
	Asset = InAsset;

#if WITH_EDITOR
	SynchronizeWithSourceSystem();
	if (Asset != nullptr)
	{
		AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
	}
#else
	if (Asset != nullptr)
	{
		CopyParametersFromAsset(bResetExistingOverrideParameters);
		OverrideParameters.Rebind();
	}
#endif

	if (Asset && IsRegistered())
	{
		if (bAutoActivate || bWasActive)
		{
			Activate();
		}
	}
}

void UNiagaraComponent::SetForceSolo(bool bInForceSolo) 
{ 
	if (bForceSolo != bInForceSolo)
	{
		bForceSolo = bInForceSolo;
		DestroyInstance();
		SetComponentTickEnabled(bInForceSolo);
	}
}

void UNiagaraComponent::SetGpuComputeDebug(bool bEnableDebug)
{
	if (bEnableGpuComputeDebug != bEnableDebug)
	{
		bEnableGpuComputeDebug = bEnableDebug;
		if (SystemInstanceController.IsValid())
		{
			SystemInstanceController->SetGpuComputeDebug(bEnableGpuComputeDebug != 0);
		}
	}
}

void UNiagaraComponent::SetCustomTimeDilation(float Dilation)
{
	if ( !bForceSolo )
	{
		UE_LOG(LogNiagara, Warning, TEXT("Attempting to set custom time dilation on a NiagaraComponent(%s) that is not in solo mode, value will be ignored."), *GetNameSafe(this));
		return;
	}
	CustomTimeDilation = Dilation;
}

void UNiagaraComponent::SetAutoAttachmentParameters(USceneComponent* Parent, FName SocketName, EAttachmentRule LocationRule, EAttachmentRule RotationRule, EAttachmentRule ScaleRule)
{
	AutoAttachParent = Parent;
	AutoAttachSocketName = SocketName;
	AutoAttachLocationRule = LocationRule;
	AutoAttachRotationRule = RotationRule;
	AutoAttachScaleRule = ScaleRule;
}


void UNiagaraComponent::CancelAutoAttachment(bool bDetachFromParent)
{
	if (bAutoManageAttachment)
	{
		if (bDidAutoAttach)
		{
			// Restore relative transform from before attachment. Actual transform will be updated as part of DetachFromParent().
			SetRelativeLocation_Direct(SavedAutoAttachRelativeLocation);
			SetRelativeRotation_Direct(SavedAutoAttachRelativeRotation);
			SetRelativeScale3D_Direct(SavedAutoAttachRelativeScale3D);
			bDidAutoAttach = false;
		}

		if (bDetachFromParent)
		{
			//bIsChangingAutoAttachment = true;
			DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
			//bIsChangingAutoAttachment = false;
		}
	}
}
