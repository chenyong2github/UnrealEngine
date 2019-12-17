// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraComponent.h"
#include "VectorVM.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstance.h"
#include "MeshBatch.h"
#include "SceneUtils.h"
#include "ComponentReregisterContext.h"
#include "NiagaraConstants.h"
#include "NiagaraStats.h"
#include "NiagaraCommon.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceStaticMesh.h"
#include "UObject/NameTypes.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraWorldManager.h"
#include "EngineUtils.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Engine/CollisionProfile.h"
#include "NiagaraEmitterInstanceBatcher.h"

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

void DumpNiagaraComponents(UWorld* World)
{
	for (TActorIterator<AActor> ActorItr(World); ActorItr; ++ActorItr)
	{
		TArray<UNiagaraComponent*> Components;
		ActorItr->GetComponents<UNiagaraComponent>(Components, true);
		if (Components.Num() != 0)
		{
			UE_LOG(LogNiagara, Log, TEXT("Actor: \"%s\" ... %d Components"), *ActorItr->GetName(), Components.Num());
		}

		for (UNiagaraComponent* Component : Components)
		{
			if (Component != nullptr)
			{
				UNiagaraSystem* Sys = Component->GetAsset();
				FNiagaraSystemInstance* SysInst = Component->GetSystemInstance();
				if (!Sys)
				{
					UE_LOG(LogNiagara, Log, TEXT("Component: \"%s\" ... no system"), *Component->GetName());

				}
				else if (Sys && !SysInst)
				{
					UE_LOG(LogNiagara, Log, TEXT("Component: \"%s\" System: \"%s\" ... no instance"), *Component->GetName(), *Sys->GetName());

				}
				else
				{
					UE_LOG(LogNiagara, Log, TEXT("Component: \"%s\" System: \"%s\" | ReqExecState: %d | ExecState: %d | bIsActive: %d"), *Component->GetName(), *Sys->GetName(),
						(int32)SysInst->GetRequestedExecutionState(), (int32)SysInst->GetActualExecutionState(), Component->IsActive());

					if (!SysInst->IsComplete())
					{
						for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Emitter : SysInst->GetEmitters())
						{
							UE_LOG(LogNiagara, Log, TEXT("    Emitter: \"%s\" | ExecState: %d | NumParticles: %d | CPUTime: %f"), *Emitter->GetEmitterHandle().GetUniqueInstanceName(),
								(int32)Emitter->GetExecutionState(), Emitter->GetNumParticles(), Emitter->GetTotalCPUTimeMS());
						}
					}
				}
			}
		}
	}
}

FAutoConsoleCommandWithWorld DumpNiagaraComponentsCommand(
	TEXT("DumpNiagaraComponents"),
	TEXT("Dump Existing Niagara Components"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&DumpNiagaraComponents)
);


FNiagaraSceneProxy::FNiagaraSceneProxy(const UNiagaraComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent, InComponent->GetAsset() ? InComponent->GetAsset()->GetFName() : FName())
		, bRenderingEnabled(true)
		, RuntimeCycleCount(nullptr)
#if WITH_PARTICLE_PERF_STATS
		, PerfAsset(InComponent->GetAsset())
#endif
{
	// In this case only, update the System renderers on the game thread.
	check(IsInGameThread());
	FNiagaraSystemInstance* SystemInst = InComponent->GetSystemInstance();
	if (SystemInst)
	{
		//UE_LOG(LogNiagara, Warning, TEXT("FNiagaraSceneProxy %p"), this);

		CreateRenderers(InComponent);
		bAlwaysHasVelocity = true;
		Batcher = SystemInst->GetBatcher();

#if STATS
		SystemStatID = InComponent->GetAsset()->GetStatID(false, false);
#endif

		RuntimeCycleCount = InComponent->GetAsset()->GetCycleCounter(false, false);
	}
}

SIZE_T FNiagaraSceneProxy::GetTypeHash() const
{
	static size_t UniquePointer;
	return reinterpret_cast<size_t>(&UniquePointer);
}

void FNiagaraSceneProxy::ReleaseRenderers()
{
	if (EmitterRenderers.Num() > 0)
	{
		//Renderers must be freed on the render thread.
		ENQUEUE_RENDER_COMMAND(ReleaseRenderersCommand)(
			[ToDeleteEmitterRenderers = MoveTemp(EmitterRenderers)](FRHICommandListImmediate& RHICmdList)
		{
			for (FNiagaraRenderer* EmitterRenderer : ToDeleteEmitterRenderers)
			{
				if (EmitterRenderer)
				{
					EmitterRenderer->ReleaseRenderThreadResources();
					delete EmitterRenderer;
				}
			}
		});
		EmitterRenderers.Empty();
	}
	RendererDrawOrder.Empty();
}

void FNiagaraSceneProxy::CreateRenderers(const UNiagaraComponent* Component)
{
	LLM_SCOPE(ELLMTag::Niagara);

	check(Component);
	check(Component->GetSystemInstance());

	UNiagaraSystem* System = Component->GetAsset();
	check(System);

	struct FSortInfo
	{
		FSortInfo(int32 InSortHint, int32 InRendererIdx): SortHint(InSortHint), RendererIdx(InRendererIdx){}
		int32 SortHint;
		int32 RendererIdx;
	};
	TArray<FSortInfo, TInlineAllocator<8>> RendererSortInfo;

	ReleaseRenderers();
	ERHIFeatureLevel::Type FeatureLevel = GetScene().GetFeatureLevel();
	for(TSharedRef<const FNiagaraEmitterInstance, ESPMode::ThreadSafe> EmitterInst : Component->GetSystemInstance()->GetEmitters())
	{
		if (UNiagaraEmitter* Emitter = EmitterInst->GetCachedEmitter())
		{
			for (UNiagaraRendererProperties* Properties : Emitter->GetEnabledRenderers())
			{
				RendererSortInfo.Emplace(Properties->SortOrderHint, EmitterRenderers.Num());
				FNiagaraRenderer* NewRenderer = nullptr;
				if (Properties->GetIsEnabled() && EmitterInst->GetData().IsInitialized() && !EmitterInst->IsDisabled())
				{
					NewRenderer = Properties->CreateEmitterRenderer(FeatureLevel, &EmitterInst.Get());
				}
				EmitterRenderers.Add(NewRenderer);
			}
		}
	}

	// We sort by the sort hint in order to guarantee that we submit according to the preferred sort order..
	RendererSortInfo.Sort([&](const FSortInfo& A, const FSortInfo& B)
	{
		int32 AIndex = A.SortHint;
		int32 BIndex = B.SortHint;
		return AIndex < BIndex;
	});
	RendererDrawOrder.Reset(RendererSortInfo.Num());
	for (FSortInfo SortInfo : RendererSortInfo)
	{
		RendererDrawOrder.Add(SortInfo.RendererIdx);
	}
}

FNiagaraSceneProxy::~FNiagaraSceneProxy()
{
	Batcher = nullptr;

	//UE_LOG(LogNiagara, Warning, TEXT("~FNiagaraSceneProxy %p"), this);
	check(IsInRenderingThread());
	for (FNiagaraRenderer* EmitterRenderer : EmitterRenderers)
	{
		if (EmitterRenderer)
		{
			EmitterRenderer->ReleaseRenderThreadResources();
			delete EmitterRenderer;
		}
	}
	EmitterRenderers.Empty();
}

void FNiagaraSceneProxy::ReleaseRenderThreadResources()
{
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Renderer->ReleaseRenderThreadResources();
		}
	}
}

// FPrimitiveSceneProxy interface.
void FNiagaraSceneProxy::CreateRenderThreadResources()
{
	LLM_SCOPE(ELLMTag::Niagara);
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Renderer->CreateRenderThreadResources(Batcher);
		}
	}
	return;
}

void FNiagaraSceneProxy::OnTransformChanged()
{
	LocalToWorldInverse = GetLocalToWorld().Inverse();
}

FPrimitiveViewRelevance FNiagaraSceneProxy::GetViewRelevance(const FSceneView* View) const
{
	FPrimitiveViewRelevance Relevance;

	if (bRenderingEnabled == false || !FNiagaraUtilities::SupportsNiagaraRendering(View->GetFeatureLevel()))
	{
		return Relevance;
	}
	Relevance.bDynamicRelevance = true;

	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Relevance |= Renderer->GetViewRelevance(View, this);
		}
	}

	Relevance.bVelocityRelevance = IsMovable() && Relevance.bOpaqueRelevance && Relevance.bRenderInMainPass;

	return Relevance;
}


uint32 FNiagaraSceneProxy::GetMemoryFootprint() const
{ 
	return (sizeof(*this) + GetAllocatedSize()); 
}

uint32 FNiagaraSceneProxy::GetAllocatedSize() const
{ 
	uint32 DynamicDataSize = 0;
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			DynamicDataSize += Renderer->GetDynamicDataSize();
		}
	}
	return FPrimitiveSceneProxy::GetAllocatedSize() + DynamicDataSize;
}

bool FNiagaraSceneProxy::GetRenderingEnabled() const
{
	return bRenderingEnabled;
}

void FNiagaraSceneProxy::SetRenderingEnabled(bool bInRenderingEnabled)
{
	bRenderingEnabled = bInRenderingEnabled;
}

void FNiagaraSceneProxy::GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_RT);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentGetDynamicMeshElements);

#if STATS
	FScopeCycleCounter SystemStatCounter(SystemStatID);
#endif

	FNiagaraScopedRuntimeCycleCounter RuntimeScope(RuntimeCycleCount);

	for (int32 RendererIdx : RendererDrawOrder)
	{
		FNiagaraRenderer* Renderer = EmitterRenderers[RendererIdx];
		if (Renderer && (Renderer->GetSimTarget() == ENiagaraSimTarget::CPUSim || ViewFamily.GetFeatureLevel() >= ERHIFeatureLevel::ES3_1))
		{
			Renderer->GetDynamicMeshElements(Views, ViewFamily, VisibilityMap, Collector, this);
		}
	}

	if (ViewFamily.EngineShowFlags.Particles)
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
	FNiagaraScopedRuntimeCycleCounter RuntimeScope(RuntimeCycleCount);
	for (FNiagaraRenderer* Renderer : EmitterRenderers)
	{
		if (Renderer)
		{
			Renderer->GetDynamicRayTracingInstances(Context, OutRayTracingInstances, this);
		}
	}
}
#endif

void FNiagaraSceneProxy::GatherSimpleLights(const FSceneViewFamily& ViewFamily, FSimpleLightArray& OutParticleLights) const
{
	FNiagaraScopedRuntimeCycleCounter RuntimeScope(RuntimeCycleCount);
	for (int32 Idx = 0; Idx < EmitterRenderers.Num(); Idx++)
	{
		FNiagaraRenderer *Renderer = EmitterRenderers[Idx];
		if (Renderer && Renderer->HasLights())
		{
			Renderer->GatherSimpleLights(OutParticleLights);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

UNiagaraComponent::UNiagaraComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, OverrideParameters(this)
	, bForceSolo(false)
	, AgeUpdateMode(ENiagaraAgeUpdateMode::TickDeltaTime)
	, DesiredAge(0.0f)
	, bCanRenderWhileSeeking(true)
	, SeekDelta(1 / 30.0f)
	, MaxSimTime(33.0f / 1000.0f)
	, bIsSeeking(false)
	, bAutoDestroy(false)
	, MaxTimeBeforeForceUpdateTransform(5.0f)
#if WITH_EDITOR
	, PreviewDetailLevel(INDEX_NONE)
	, PreviewLODDistance(0.0f)
	, bEnablePreviewDetailLevel(false)
	, bEnablePreviewLODDistance(false)
	, bWaitForCompilationOnActivate(false)
#endif
	, bAwaitingActivationDueToNotReady(false)
	, bActivateShouldResetWhenReady(false)
	, bDidAutoAttach(false)
	, bAllowScalability(true)
	//, bIsChangingAutoAttachment(false)
	, ScalabilityManagerHandle(INDEX_NONE)
	, OwnerLOD(0)
{
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
}

/********* UFXSystemComponent *********/
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

void UNiagaraComponent::SetEmitterEnable(FName EmitterName, bool bNewEnableState)
{
	if (!SystemInstance)
	{
		return;
	}
	if (SystemInstance.Get() && !SystemInstance->IsComplete())
	{
		SystemInstance->SetEmitterEnable(EmitterName, bNewEnableState);
	}
}

void UNiagaraComponent::ReleaseToPool()
{
	if (PoolingMethod != ENCPoolMethod::ManualRelease)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Manually releasing a PSC to the pool that was not spawned with ENCPoolMethod::ManualRelease. Asset=%s Component=%s"),
			Asset ? *Asset->GetPathName() : TEXT("NULL"), *GetPathName()
		);
		return;
	}

	if (!IsActive())
	{
		//If we're already complete then release to the pool straight away.
		UWorld* World = GetWorld();
		check(World);
		FNiagaraWorldManager::Get(World)->GetComponentPool()->ReclaimWorldParticleSystem(this);
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

/********* UFXSystemComponent *********/


void UNiagaraComponent::TickComponent(float DeltaSeconds, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	LLM_SCOPE(ELLMTag::Niagara);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentTick);

	FScopeCycleCounter SystemStatCounter(Asset ? Asset->GetStatID(true, false) : TStatId());

	if (bAwaitingActivationDueToNotReady)
	{
		Activate(bActivateShouldResetWhenReady);
		return;
	}

	if (!SystemInstance)
	{
		return;
	}

	if (!IsActive() && bAutoActivate && SystemInstance.Get() && SystemInstance->GetAreDataInterfacesInitialized())
	{
		Activate();
	}

	check(SystemInstance->IsSolo());
	if (IsActive() && SystemInstance.Get() && !SystemInstance->IsComplete())
	{
		// If the interfaces have changed in a meaningful way, we need to potentially rebind and update the values.
		if (OverrideParameters.GetInterfacesDirty())
		{
			SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ReInit);
		}

		if (AgeUpdateMode == ENiagaraAgeUpdateMode::TickDeltaTime)
		{
			SystemInstance->ComponentTick(DeltaSeconds, ThisTickFunction->GetCompletionHandle());
		}
		else
		{
			float AgeDiff = FMath::Max(DesiredAge, 0.0f) - SystemInstance->GetAge();
			int32 TicksToProcess = 0;
			if (FMath::Abs(AgeDiff) < KINDA_SMALL_NUMBER)
			{
				AgeDiff = 0.0f;
			}
			else
			{
				if (AgeDiff < 0.0f)
				{
					SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
					AgeDiff = DesiredAge - SystemInstance->GetAge();
				}

				if (AgeDiff > 0.0f)
				{
					FNiagaraSystemSimulation* SystemSim = GetSystemSimulation().Get();
					if (SystemSim)
					{
						double StartTime = FPlatformTime::Seconds();
						double CurrentTime = StartTime;

						TicksToProcess = FMath::FloorToInt(AgeDiff / SeekDelta);
						for (; TicksToProcess > 0 && CurrentTime - StartTime < MaxSimTime; --TicksToProcess)
						{
							//Cannot do multiple tick off the game thread here without additional work. So we pass in null for the completion event which will force GT execution.
							//If this becomes a perf problem I can add a new path for the tick code to handle multiple ticks.
							SystemInstance->ComponentTick(SeekDelta, nullptr);
							CurrentTime = FPlatformTime::Seconds();
						}
					}
				}
			}

			if (TicksToProcess == 0)
			{
				bIsSeeking = false;
			}
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
	DestroyInstance();
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
	if (SystemInstance.IsValid() && TickDeltaSeconds > SMALL_NUMBER)
	{
		SystemInstance->AdvanceSimulation(TickCount, TickDeltaSeconds);
	}
}

void UNiagaraComponent::AdvanceSimulationByTime(float SimulateTime, float TickDeltaSeconds)
{
	if (SystemInstance.IsValid() && TickDeltaSeconds > SMALL_NUMBER)
	{
		int32 TickCount = SimulateTime / TickDeltaSeconds;
		SystemInstance->AdvanceSimulation(TickCount, TickDeltaSeconds);
	}
}

void UNiagaraComponent::SetPaused(bool bInPaused)
{
	if (SystemInstance.IsValid())
	{
		SystemInstance->SetPaused(bInPaused);
	}
}

bool UNiagaraComponent::IsPaused()const
{
	if (SystemInstance.IsValid())
	{
		return SystemInstance->IsPaused();
	}
	return false;
}

UNiagaraDataInterface * UNiagaraComponent::GetDataInterface(const FString &Name)
{

	// @todo-threadsafety Think of a better way to do this!
	if (!SystemInstance || SystemInstance->GetEmitters().Num() == 0 || !SystemInstance->GetEmitters()[0]->GetGPUContext())
	{
		return nullptr;
	}
	
	FNiagaraComputeExecutionContext* GPUContext = SystemInstance->GetEmitters()[0]->GetGPUContext();
	const TArray<FNiagaraScriptDataInterfaceCompileInfo> &DataInterfaceInfo = GPUContext->GPUScript->GetVMExecutableData().DataInterfaceInfo;
	const TArray<UNiagaraDataInterface*>& DataInterfaces = GPUContext->CombinedParamStore.GetDataInterfaces();

	int Index = 0;
	for (UNiagaraDataInterface* Interface : DataInterfaces)
	{
		if (DataInterfaceInfo[Index].Name.GetPlainNameString() == Name)
		{			
			return Interface;
		}	
		++Index;
	}
	return nullptr;
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
	LLM_SCOPE(ELLMTag::Niagara);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	if (SystemInstance.IsValid() == false)
	{
		FNiagaraSystemInstance::AllocateSystemInstance(this, SystemInstance);
		//UE_LOG(LogNiagara, Log, TEXT("Create System: %p | %s\n"), SystemInstance.Get(), *GetAsset()->GetFullName());
#if WITH_EDITORONLY_DATA
		OnSystemInstanceChangedDelegate.Broadcast();
#endif
		SystemInstance->Init(bForceSolo);
		MarkRenderStateDirty();
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
	bAwaitingActivationDueToNotReady = false;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (IsES2Platform(GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel]))
	{
		GbSuppressNiagaraSystems = 1;
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (GbSuppressNiagaraSystems != 0)
	{
		OnSystemComplete();
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentActivate);
	if (Asset == nullptr)
	{
		DestroyInstance();
		if (!HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject | RF_ClassDefaultObject))
		{
			UE_LOG(LogNiagara, Warning, TEXT("Failed to activate Niagara Component due to missing or invalid asset! (%s)"), *GetFullName());
		}
		SetComponentTickEnabled(false);
		return;
	}
	
	// If the particle system can't ever render (ie on dedicated server or in a commandlet) than do not activate...
	if (!FApp::CanEverRender())
	{
		return;
	}

	if (!IsRegistered())
	{
		return;
	}

	// On the off chance that the user changed the asset, we need to clear out the existing data.
	if (SystemInstance.IsValid() && SystemInstance->GetSystem() != Asset)
	{
		OnSystemComplete();
	}

#if WITH_EDITOR
	// In case we're not yet ready to run due to compilation requests, go ahead and keep polling there..
	if (Asset->HasOutstandingCompilationRequests())
	{
		if (bWaitForCompilationOnActivate)
		{
			Asset->WaitForCompilationComplete();
		}
		Asset->PollForCompilationComplete();
	}
#endif
	
	if (!Asset->IsReadyToRun() || !IsWorldReadyToRun())
	{
		bAwaitingActivationDueToNotReady = true;
		bActivateShouldResetWhenReady = bReset;
		SetComponentTickEnabled(true);
		return;
	}

	if (bReset)
	{
		UnregisterWithScalabilityManager();
	}

	if (!bIsScalabilityCull && ScalabilityManagerHandle != INDEX_NONE)
	{
		//If this is a non scalability activate call and we're still registered with the manager.
		//If we reach this point then we must have been previously culled by scalability so bail here.
		return;
	}

	if (RegisterWithScalabilityManagerOrPreCull())
	{
		//We have decided to pre cull the system.
		return;
	}

	Super::Activate(bReset);

	// Early out if we're not forcing a reset, and both the component and system instance are already active.
	if (bReset == false &&
		IsActive() &&
		SystemInstance != nullptr &&
		SystemInstance->GetRequestedExecutionState() == ENiagaraExecutionState::Active &&
		SystemInstance->GetActualExecutionState() == ENiagaraExecutionState::Active)
	{
		return;
	}

	//UE_LOG(LogNiagara, Log, TEXT("Activate: %u - %s"), this, *Asset->GetName());
	
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
				AttachToComponent(NewParent, FAttachmentTransformRules(AutoAttachLocationRule, AutoAttachRotationRule, AutoAttachScaleRule, false), AutoAttachSocketName);
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

	FNiagaraSystemInstance::EResetMode ResetMode = FNiagaraSystemInstance::EResetMode::ResetSystem;
	if (InitializeSystem())
	{
		ResetMode = FNiagaraSystemInstance::EResetMode::None;//Already done a reinit sete
	}

	if (!SystemInstance)
	{
		return;
	}

	SystemInstance->Activate(ResetMode);

	/** We only need to tick the component if we require solo mode. */
	SetComponentTickEnabled(SystemInstance->IsSolo());
}

void UNiagaraComponent::Deactivate()
{
	DeactivateInternal(false);
}

void UNiagaraComponent::DeactivateInternal(bool bIsScalabilityCull /* = false */)
{
	if (IsActive())
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentDeactivate);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);

		//UE_LOG(LogNiagara, Log, TEXT("Deactivate: %u - %s"), this, *Asset->GetName());

		if (SystemInstance)
		{
			// Don't deactivate in solo mode as we are not ticked by the world but rather the component
			// Deactivating will cause the system to never Complete
			if (SystemInstance->IsSolo() == false)
			{
				Super::Deactivate();
			}

			SystemInstance->Deactivate();

			// We are considered active until we are complete
			SetActiveFlag(!SystemInstance->IsComplete());
		}
		else
		{
			Super::Deactivate();
			SetActiveFlag(false);
		}

		//Unregister with the scalability manager if this is a genuine deactivation from outside.
		//The scalability manager itself can call this function when culling systems.
		if (bIsScalabilityCull == false)
		{
			UnregisterWithScalabilityManager();
		}

		SystemInstance->Deactivate();
	}
	else
	{
		Super::Deactivate();
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

	//UE_LOG(LogNiagara, Log, TEXT("DeactivateImmediate: %u - %s"), this, *Asset->GetName());

	//UE_LOG(LogNiagara, Log, TEXT("Deactivate %s"), *GetName());

	//Unregister with the scalability manager if this is a genuine deactivation from outside.
	//The scalability manager itself can call this function when culling systems.
	if (bIsScalabilityCull == false)
	{
		UnregisterWithScalabilityManager();
	}

	SetActiveFlag(false);

	if (SystemInstance)
	{
		SystemInstance->Deactivate(true);
	}
}

bool UNiagaraComponent::RegisterWithScalabilityManagerOrPreCull()
{
	if (ScalabilityManagerHandle == INDEX_NONE && bAllowScalability)
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
					else
					{
						WorldMan->RegisterWithScalabilityManager(this);
					}
				}
			}
		}
	}

	return false;
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
	ScalabilityManagerHandle = INDEX_NONE;//Just to be sure our state is unregistered.
}

void UNiagaraComponent::OnSystemComplete()
{
	//UE_LOG(LogNiagara, Log, TEXT("OnSystemComplete: %p - %s"), SystemInstance.Get(), *Asset->GetName());

	SetComponentTickEnabled(false);
	SetActiveFlag(false);

	MarkRenderDynamicDataDirty();
		
	//UE_LOG(LogNiagara, Log, TEXT("OnSystemFinished.Broadcast(this);: { %p - %s"), SystemInstance.Get(), *Asset->GetName());
	OnSystemFinished.Broadcast(this);
	//UE_LOG(LogNiagara, Log, TEXT("OnSystemFinished.Broadcast(this);: } %p - %s"), SystemInstance.Get(), *Asset->GetName());

	if (PoolingMethod == ENCPoolMethod::AutoRelease)
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
	else if (bAutoManageAttachment && ScalabilityManagerHandle == INDEX_NONE)//Do not detach from our parent if we were deactivated by scalability and we need to be considered for reactivation.
	{
		CancelAutoAttachment(/*bDetachFromParent=*/ true);
	}
}

void UNiagaraComponent::DestroyInstance()
{
	//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::DestroyInstance: %p  %s\n"), SystemInstance.Get(), *GetAsset()->GetFullName());
	//UE_LOG(LogNiagara, Log, TEXT("DestroyInstance: %u - %s"), this, *Asset->GetName());
	SetActiveFlag(false);

	UnregisterWithScalabilityManager();
	
	// Rather than setting the unique ptr to null here, we allow it to transition ownership to the system's deferred deletion queue. This allows us to safely
	// get rid of the system interface should we be doing this in response to a callback invoked during the system interface's lifetime completion cycle.
	FNiagaraSystemInstance::DeallocateSystemInstance(SystemInstance); // System Instance will be nullptr after this.
	check(SystemInstance.Get() == nullptr);


#if WITH_EDITORONLY_DATA
	OnSystemInstanceChangedDelegate.Broadcast();
#endif
	MarkRenderStateDirty();
}

void UNiagaraComponent::OnRegister()
{
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
	Super::OnRegister();
}

void UNiagaraComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	//UE_LOG(LogNiagara, Log, TEXT("OnComponentDestroyed %p %p"), this, SystemInstance.Get());
	//DestroyInstance();//Can't do this here as we can call this from inside the system instance currently during completion 

	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UNiagaraComponent::OnUnregister()
{
	Super::OnUnregister();

	SetActiveFlag(false);

	if (SystemInstance)
	{
		//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::OnUnregister: %p  %s\n"), SystemInstance.Get(), *GetAsset()->GetFullName());

		SystemInstance->Deactivate(true);

		// Rather than setting the unique ptr to null here, we allow it to transition ownership to the system's deferred deletion queue. This allows us to safely
		// get rid of the system interface should we be doing this in response to a callback invoked during the system interface's lifetime completion cycle.
		FNiagaraSystemInstance::DeallocateSystemInstance(SystemInstance); // System Instance will be nullptr after this.
		check(SystemInstance.Get() == nullptr);
#if WITH_EDITORONLY_DATA
		OnSystemInstanceChangedDelegate.Broadcast();
#endif
	}
}

void UNiagaraComponent::BeginDestroy()
{
	//UE_LOG(LogNiagara, Log, TEXT("UNiagaraComponent::BeginDestroy(): %0xP  %s\n"), this, *GetAsset()->GetFullName());
	DestroyInstance();

	Super::BeginDestroy();
}

TSharedPtr<FNiagaraSystemSimulation, ESPMode::ThreadSafe> UNiagaraComponent::GetSystemSimulation()
{
	if (SystemInstance)
	{
		return SystemInstance->GetSystemSimulation();
	}

	return nullptr;
}

void UNiagaraComponent::OnEndOfFrameUpdateDuringTick()
{
	Super::OnEndOfFrameUpdateDuringTick();
	if ( SystemInstance )
	{
		SystemInstance->WaitForAsyncTick();
	}
}

void UNiagaraComponent::CreateRenderState_Concurrent()
{
	Super::CreateRenderState_Concurrent();
	// The emitter instance may not tick again next frame so we send the dynamic data here so that the current state
	// renders.  This can happen when while editing, or any time the age update mode is set to desired age.
	SendRenderDynamicData_Concurrent();
}

void UNiagaraComponent::SendRenderDynamicData_Concurrent()
{
	LLM_SCOPE(ELLMTag::Niagara);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraComponentSendRenderData);
	PARTICLE_PERF_STAT_CYCLES(Asset, EndOfFrame);

	Super::SendRenderDynamicData_Concurrent();

	if (SystemInstance.IsValid() && SceneProxy)
	{
#if STATS
		TStatId SystemStatID = GetAsset() ? GetAsset()->GetStatID(true, true) : TStatId();
		FScopeCycleCounter SystemStatCounter(SystemStatID);
#endif

		FNiagaraScopedRuntimeCycleCounter RuntimeScope(GetAsset(), true, false);

		FNiagaraSceneProxy* NiagaraProxy = static_cast<FNiagaraSceneProxy*>(SceneProxy);
		const TArray<FNiagaraRenderer*>& EmitterRenderers = NiagaraProxy->GetEmitterRenderers();

		typedef TArray<FNiagaraDynamicDataBase*, TInlineAllocator<8>> TDynamicDataArray;
		TDynamicDataArray NewDynamicData;
		NewDynamicData.Reserve(EmitterRenderers.Num());

		int32 RendererIndex = 0;
		for (int32 i = 0; i < SystemInstance->GetEmitters().Num(); i++)
		{
			FNiagaraEmitterInstance* EmitterInst = &SystemInstance->GetEmitters()[i].Get();
			UNiagaraEmitter* Emitter = EmitterInst->GetCachedEmitter();

#if STATS
			TStatId EmitterStatID = Emitter->GetStatID(true, true);
			FScopeCycleCounter EmitterStatCounter(EmitterStatID);
#endif

			const TArray<UNiagaraRendererProperties*>& Renderers = Emitter->GetEnabledRenderers();
			for (int32 EmitterIdx = 0; EmitterIdx < Renderers.Num(); EmitterIdx++, RendererIndex++)
			{
				UNiagaraRendererProperties* Properties = Renderers[EmitterIdx];
				FNiagaraRenderer* Renderer = EmitterRenderers[RendererIndex];
				FNiagaraDynamicDataBase* NewData = nullptr;
				
				if (Renderer)
				{
					bool bRendererEditorEnabled = true;
#if WITH_EDITORONLY_DATA
					const FNiagaraEmitterHandle& Handle = Asset->GetEmitterHandle(i);
					bRendererEditorEnabled = (!SystemInstance->GetIsolateEnabled() || Handle.IsIsolated());
#endif
					if (bRendererEditorEnabled && !EmitterInst->IsComplete() && !SystemInstance->IsComplete())
					{
						NewData = Renderer->GenerateDynamicData(NiagaraProxy, Properties, EmitterInst);
					}
				}

				NewDynamicData.Add(NewData);
			}
		}

#if WITH_EDITOR
		if (EmitterRenderers.Num() != NewDynamicData.Num())
		{
			// This can happen in the editor when modifying the number or renderers while the system is running and the render thread is already processing the data.
			// in this case we just skip drawing this frame since the system will be reinitialized.
			return;
		}
#endif
		
		ENQUEUE_RENDER_COMMAND(NiagaraSetDynamicData)(
			[NiagaraProxy, DynamicData = MoveTemp(NewDynamicData), PerfAsset=Asset](FRHICommandListImmediate& RHICmdList)
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraSetDynamicData);
			PARTICLE_PERF_STAT_CYCLES(PerfAsset, RenderUpdate);

			const TArray<FNiagaraRenderer*>& EmitterRenderers_RT = NiagaraProxy->GetEmitterRenderers();
			for (int32 i = 0; i < EmitterRenderers_RT.Num(); ++i)
			{
				if (FNiagaraRenderer* Renderer = EmitterRenderers_RT[i])
				{
					Renderer->SetDynamicData_RenderThread(DynamicData[i]);
				}
			}
		});
	}
}

int32 UNiagaraComponent::GetNumMaterials() const
{
	TArray<UMaterialInterface*> UsedMaterials;
	if (SystemInstance)
	{
		for (int32 i = 0; i < SystemInstance->GetEmitters().Num(); i++)
		{
			FNiagaraEmitterInstance* EmitterInst = &SystemInstance->GetEmitters()[i].Get();
			UNiagaraEmitter* Emitter = EmitterInst->GetCachedEmitter();
			for (UNiagaraRendererProperties* Properties : Emitter->GetEnabledRenderers())
			{
				Properties->GetUsedMaterials(EmitterInst, UsedMaterials);
			}
		}
	}

	return UsedMaterials.Num();
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
	if (SystemInstance.IsValid())
	{
		SystemBounds = SystemInstance->GetLocalBounds();
	}
	else
	{
		FBox SimBounds(ForceInit);
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
	if (!SystemInstance.IsValid())
	{
		return;
	}

	for (int32 EmitterIdx = 0; EmitterIdx < SystemInstance->GetEmitters().Num(); ++EmitterIdx)
	{
		TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe> Sim = SystemInstance->GetEmitters()[EmitterIdx];

		if (UNiagaraEmitter* Props = Sim->GetEmitterHandle().GetInstance())
		{
			if (Props)
			{
				for (UNiagaraRendererProperties* Renderer : Props->GetEnabledRenderers())
				{
					Renderer->GetUsedMaterials(&Sim.Get(), OutMaterials);
				}
			}
		}
	}
}

void UNiagaraComponent::SetComponentTickEnabled(bool bEnabled)
{
	Super::SetComponentTickEnabled(bEnabled);
	if (SystemInstance.IsValid())
	{
		SystemInstance->UpdatePrereqs();
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
	if ( SystemInstance.IsValid() )
	{
		SystemInstance->UpdatePrereqs();
	}
}

void UNiagaraComponent::OnChildAttached(USceneComponent* ChildComponent)
{
	Super::OnChildAttached(ChildComponent);
	if (SystemInstance.IsValid())
	{
		SystemInstance->UpdatePrereqs();
	}
}

void UNiagaraComponent::OnChildDetached(USceneComponent* ChildComponent)
{
	Super::OnChildDetached(ChildComponent);
	if (SystemInstance.IsValid())
	{
		SystemInstance->UpdatePrereqs();
	}
}

FNiagaraSystemInstance* UNiagaraComponent::GetSystemInstance() const
{
	return SystemInstance.Get();
}

void UNiagaraComponent::SetVariableLinearColor(FName InVariableName, const FLinearColor& InValue)
{
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableLinearColor(const FString& InVariableName, const FLinearColor& InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetColorDef(), VarName), true);
}


void UNiagaraComponent::SetVariableQuat(FName InVariableName, const FQuat& InValue)
{
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableQuat(const FString& InVariableName, const FQuat& InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), VarName), true);
}

void UNiagaraComponent::SetVariableVec4(FName InVariableName, const FVector4& InValue)
{
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableVec4(const FString& InVariableName, const FVector4& InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec4Def(), VarName), true);
}

void UNiagaraComponent::SetVariableVec3(FName InVariableName, FVector InValue)
{
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableVec3(const FString& InVariableName, FVector InValue)
{
	FName VarName = FName(*InVariableName);
	
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), VarName), true);
}

void UNiagaraComponent::SetVariableVec2(FName InVariableName, FVector2D InValue)
{
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(),InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableVec2(const FString& InVariableName, FVector2D InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetVec2Def(),VarName), true);
}

void UNiagaraComponent::SetVariableFloat(FName InVariableName, float InValue)
{
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableFloat(const FString& InVariableName, float InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), VarName), true);
}

void UNiagaraComponent::SetVariableInt(FName InVariableName, int32 InValue)
{
	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableInt(const FString& InVariableName, int32 InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetParameterValue(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), VarName), true);
}

void UNiagaraComponent::SetVariableBool(FName InVariableName, bool InValue)
{
	OverrideParameters.SetParameterValue(InValue ? FNiagaraBool::True : FNiagaraBool::False, FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), InVariableName), true);
}

void UNiagaraComponent::SetNiagaraVariableBool(const FString& InVariableName, bool InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetParameterValue(InValue ? FNiagaraBool::True : FNiagaraBool::False, FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), VarName), true);
}

void UNiagaraComponent::SetNiagaraVariableActor(const FString& InVariableName, AActor* InValue)
{
	SetNiagaraVariableObject(InVariableName, InValue);
}

void UNiagaraComponent::SetVariableActor(FName InVariableName, AActor* InValue)
{
	SetVariableObject(InVariableName, InValue);
}

void UNiagaraComponent::SetNiagaraVariableObject(const FString& InVariableName, UObject* InValue)
{
	FName VarName = FName(*InVariableName);

	OverrideParameters.SetUObject(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetUObjectDef(), VarName));
}

void UNiagaraComponent::SetVariableObject(FName InVariableName, UObject* InValue)
{
	OverrideParameters.SetUObject(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetUObjectDef(), InVariableName));
}

void UNiagaraComponent::SetVariableMaterial(FName InVariableName, UMaterialInterface* InValue)
{
	OverrideParameters.SetUObject(InValue, FNiagaraVariable(FNiagaraTypeDefinition::GetUMaterialDef(), InVariableName));
	MarkRenderStateDirty(); // Materials might be using this on the system, so invalidate the render state to re-gather them.
}

TArray<FVector> UNiagaraComponent::GetNiagaraParticlePositions_DebugOnly(const FString& InEmitterName)
{
	return GetNiagaraParticleValueVec3_DebugOnly(InEmitterName, TEXT("Position"));
}

TArray<FVector> UNiagaraComponent::GetNiagaraParticleValueVec3_DebugOnly(const FString& InEmitterName, const FString& InValueName)
{
	TArray<FVector> Positions;
	FName EmitterName = FName(*InEmitterName);
	if (SystemInstance.IsValid())
	{
		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& Sim : SystemInstance->GetEmitters())
		{
			if (Sim->GetEmitterHandle().GetName() == EmitterName)
			{
				FNiagaraDataBuffer& ParticleData = Sim->GetData().GetCurrentDataChecked();
				int32 NumParticles = ParticleData.GetNumInstances();
				Positions.SetNum(NumParticles);
				FNiagaraDataSetAccessor<FVector> PosData(Sim->GetData(), FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), *InValueName));
				for (int32 i = 0; i < NumParticles; ++i)
				{
					FVector Position;
					PosData.Get(i, Position);
					Positions[i] = Position;
				}
			}
		}
	}
	return Positions;

}

TArray<float> UNiagaraComponent::GetNiagaraParticleValues_DebugOnly(const FString& InEmitterName, const FString& InValueName)
{
	TArray<float> Values;
	FName EmitterName = FName(*InEmitterName);
	if (SystemInstance.IsValid())
	{
		for (TSharedRef<FNiagaraEmitterInstance, ESPMode::ThreadSafe>& Sim : SystemInstance->GetEmitters())
		{
			if (Sim->GetEmitterHandle().GetName() == EmitterName)
			{
				FNiagaraDataBuffer& ParticleData = Sim->GetData().GetCurrentDataChecked();
				int32 NumParticles = ParticleData.GetNumInstances();
				Values.SetNum(NumParticles);
				FNiagaraDataSetAccessor<float> ValueData(Sim->GetData(), FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), *InValueName));
				for (int32 i = 0; i < NumParticles; ++i)
				{
					float Value;
					ValueData.Get(i, Value);
					Values[i] = Value;
				}
			}
		}
	}
	return Values;
}

void UNiagaraComponent::PostLoad()
{
	Super::PostLoad();

	OverrideParameters.PostLoad();

	if (Asset)
	{
		Asset->ConditionalPostLoad();
#if WITH_EDITOR
		OverrideParameters.SanityCheckData();
		PostLoadNormalizeOverrideNames();
		SynchronizeWithSourceSystem();
		AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
			FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
#endif
	}
}

void UNiagaraComponent::SetUserParametersToDefaultValues()
{
	OverrideParameters.Empty();
	if (Asset == nullptr)
	{
		return;
	}

	TArray<FNiagaraVariable> SourceVars;
	Asset->GetExposedParameters().GetParameters(SourceVars);
	for (FNiagaraVariable& Param : SourceVars)
	{
		OverrideParameters.AddParameter(Param, true);
	}

	TArray<FNiagaraVariable> ExistingVars;
	OverrideParameters.GetUserParameters(ExistingVars);
	for (FNiagaraVariable ExistingVar : ExistingVars)
	{
		Asset->GetExposedParameters().CopyParameterData(OverrideParameters, ExistingVar);
	}

	OverrideParameters.Rebind();
}

#if WITH_EDITOR

void UNiagaraComponent::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange != nullptr && PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UNiagaraComponent, Asset) && Asset != nullptr)
	{
		Asset->GetExposedParameters().RemoveOnChangedHandler(AssetExposedParametersChangedHandle);
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

	ReinitializeSystem();

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UNiagaraComponent::OverrideUObjectParameter(const FNiagaraVariable& InVar, UObject* InObj)
{
	FNiagaraUserRedirectionParameterStore& ParamStore = GetOverrideParameters();
	ParamStore.SetUObject(InObj, InVar);
	SetParameterValueOverriddenLocally(InVar, true, false);
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
		OverrideParameters.Empty();
		EditorOverridesValue.Empty();
#if WITH_EDITORONLY_DATA
		OnSynchronizedWithAssetParametersDelegate.Broadcast();
#endif
		return;
	}

	TArray<FNiagaraVariable> SourceVars;
	Asset->GetExposedParameters().GetParameters(SourceVars);
	for (FNiagaraVariable& Param : SourceVars)
	{
		OverrideParameters.AddParameter(Param, true);
	}

	TArray<FNiagaraVariable> ExistingVars;
	OverrideParameters.GetUserParameters(ExistingVars);
	Asset->GetExposedParameters().GetUserParameters(SourceVars);

	for (FNiagaraVariable ExistingVar : ExistingVars)
	{
		if (!SourceVars.Contains(ExistingVar))
		{
			OverrideParameters.RemoveParameter(ExistingVar);
			EditorOverridesValue.Remove(ExistingVar.GetName());
		}
	}

	for (FNiagaraVariable ExistingVar : ExistingVars)
	{
		bool* FoundVar = EditorOverridesValue.Find(ExistingVar.GetName());

		if (!IsParameterValueOverriddenLocally(ExistingVar.GetName()))
		{
			Asset->GetExposedParameters().CopyParameterData(OverrideParameters, ExistingVar);
		}
	}

	OverrideParameters.Rebind();

#if WITH_EDITORONLY_DATA
	OnSynchronizedWithAssetParametersDelegate.Broadcast();
#endif
}

void UNiagaraComponent::AssetExposedParametersChanged()
{
	SynchronizeWithSourceSystem();
	ReinitializeSystem();
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

float UNiagaraComponent::GetMaxSimTime() const
{
	return MaxSimTime;
}

void UNiagaraComponent::SetMaxSimTime(float InMaxTime)
{
	MaxSimTime = InMaxTime;
}

#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
void UNiagaraComponent::SetPreviewDetailLevel(bool bInEnablePreviewDetailLevel, int32 InPreviewDetailLevel)
{
	bool bReInit = bEnablePreviewDetailLevel != bInEnablePreviewDetailLevel || (bEnablePreviewDetailLevel && PreviewDetailLevel != InPreviewDetailLevel);

	bEnablePreviewDetailLevel = bInEnablePreviewDetailLevel;
	PreviewDetailLevel = InPreviewDetailLevel;
	if (bReInit)
	{
		ReinitializeSystem();
	}
}

void UNiagaraComponent::SetPreviewLODDistance(bool bInEnablePreviewLODDistance, float InPreviewLODDistance)
{
	bEnablePreviewLODDistance = bInEnablePreviewLODDistance;
	PreviewLODDistance = InPreviewLODDistance;
}
#else
void UNiagaraComponent::SetPreviewDetailLevel(bool bInEnablePreviewDetailLevel, int32 InPreviewDetailLevel){}
void UNiagaraComponent::SetPreviewLODDistance(bool bInEnablePreviewLODDistance, float InPreviewLODDistance){}
#endif

void UNiagaraComponent::SetOwnerLOD(int32 InOwnerLOD)
{
	OwnerLOD = InOwnerLOD;  
}

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
	for (TPair<FName, bool> Pair : EditorOverridesValue)
	{
		bool IsOldUserParam = Pair.Key.ToString().StartsWith(TEXT("User."));
		FName ValueName = IsOldUserParam ? (*Pair.Key.ToString().RightChop(5)) : Pair.Key;
		ValueMap.Add(ValueName, Pair.Value);
	}
	EditorOverridesValue = ValueMap;
}

bool UNiagaraComponent::IsParameterValueOverriddenLocally(const FName& InParamName)
{
	bool* FoundVar = EditorOverridesValue.Find(InParamName);

	if (FoundVar != nullptr && *(FoundVar))
	{
		return true;
	}
	return false;
}

void UNiagaraComponent::SetParameterValueOverriddenLocally(const FNiagaraVariable& InParam, bool bInOverriden, bool bRequiresSystemInstanceReset)
{
	bool* FoundVar = EditorOverridesValue.Find(InParam.GetName());

	if (FoundVar != nullptr && bInOverriden) 
	{
		*(FoundVar) = bInOverriden;
	}
	else if (FoundVar == nullptr && bInOverriden)			
	{
		EditorOverridesValue.Add(InParam.GetName(), true);
	}
	else
	{
		EditorOverridesValue.Remove(InParam.GetName());
		Asset->GetExposedParameters().CopyParameterData(OverrideParameters, InParam);
	}
	
	if (InParam.IsUObject() && InParam.GetType().GetClass()->IsChildOf(UMaterialInterface::StaticClass()))
	{
		MarkRenderStateDirty();
	}

	if (bRequiresSystemInstanceReset && SystemInstance)
	{
		SystemInstance->Reset(FNiagaraSystemInstance::EResetMode::ResetAll);
	}
	
}


#endif // WITH_EDITOR



void UNiagaraComponent::SetAsset(UNiagaraSystem* InAsset)
{
	if (Asset != InAsset)
	{
#if WITH_EDITOR
		if (Asset != nullptr)
		{
			Asset->GetExposedParameters().RemoveOnChangedHandler(AssetExposedParametersChangedHandle);
		}
#endif
		Asset = InAsset;

#if WITH_EDITOR
		SynchronizeWithSourceSystem();
		if (Asset != nullptr)
		{
			AssetExposedParametersChangedHandle = Asset->GetExposedParameters().AddOnChangedHandler(
				FNiagaraParameterStore::FOnChanged::FDelegate::CreateUObject(this, &UNiagaraComponent::AssetExposedParametersChanged));
		}
		else
		{
			AssetExposedParametersChangedHandle.Reset();
		}
#else
		// We need to populate the override parameters here
		{
			TArray<FNiagaraVariable> SourceVars;
			Asset->GetExposedParameters().GetParameters(SourceVars);
			for (FNiagaraVariable& Param : SourceVars)
			{
				OverrideParameters.AddParameter(Param, true);
			}

			TArray<FNiagaraVariable> ExistingVars;
			OverrideParameters.GetUserParameters(ExistingVars);
			Asset->GetExposedParameters().GetUserParameters(SourceVars);

			for (FNiagaraVariable ExistingVar : ExistingVars)
			{
				if (SourceVars.Contains(ExistingVar))
				{
					Asset->GetExposedParameters().CopyParameterData(OverrideParameters, ExistingVar);
				}
				else
				{
					OverrideParameters.RemoveParameter(ExistingVar);
				}
			}

			OverrideParameters.Rebind();
		}
#endif

		//Force a reinit.
		DestroyInstance();
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
