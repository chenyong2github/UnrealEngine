// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraWorldManager.h"
#include "NiagaraModule.h"
#include "Modules/ModuleManager.h"
#include "NiagaraTypes.h"
#include "NiagaraEvents.h"
#include "NiagaraSettings.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraSystemInstance.h"
#include "Scalability.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "EngineModule.h"
#include "NiagaraStats.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraComponentPool.h"
#include "NiagaraComponent.h"
#include "NiagaraEffectType.h"
#include "NiagaraDebugHud.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Particles/FXBudget.h"

#if WITH_EDITORONLY_DATA
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#endif

DECLARE_CYCLE_STAT(TEXT("Niagara Manager Update Scalability Managers [GT]"), STAT_UpdateScalabilityManagers, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara Manager Tick [GT]"), STAT_NiagaraWorldManTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara Manager Wait On Render [GT]"), STAT_NiagaraWorldManWaitOnRender, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara Manager Wait Pre Garbage Collect [GT]"), STAT_NiagaraWorldManWaitPreGC, STATGROUP_Niagara);

static int GNiagaraAllowAsyncWorkToEndOfFrame = 1;
static FAutoConsoleVariableRef CVarNiagaraAllowAsyncWorkToEndOfFrame(
	TEXT("fx.Niagara.AllowAsyncWorkToEndOfFrame"),
	GNiagaraAllowAsyncWorkToEndOfFrame,
	TEXT("Allow async work to continue until the end of the frame, if false it will complete within the tick group it's started in."),
	ECVF_Default
); 

static int GNiagaraWaitOnPreGC = 1;
static FAutoConsoleVariableRef CVarNiagaraWaitOnPreGC(
	TEXT("fx.Niagara.WaitOnPreGC"),
	GNiagaraWaitOnPreGC,
	TEXT("Toggles whether Niagara will wait for all async tasks to complete before any GC calls."),
	ECVF_Default
);

static int GNiagaraSpawnPerTickGroup = 1;
static FAutoConsoleVariableRef CVarNiagaraSpawnPerTickGroup(
	TEXT("fx.Niagara.WorldManager.SpawnPerTickGroup"),
	GNiagaraSpawnPerTickGroup,
	TEXT("Will attempt to spawn new systems earlier (default enabled)."),
	ECVF_Default
);

int GNigaraAllowPrimedPools = 1;
static FAutoConsoleVariableRef CVarNigaraAllowPrimedPools(
	TEXT("fx.Niagara.AllowPrimedPools"),
	GNigaraAllowPrimedPools,
	TEXT("Allow Niagara pools to be primed."),
	ECVF_Default
);

static int32 GbAllowVisibilityCullingForDynamicBounds = 1;
static FAutoConsoleVariableRef CVarAllowVisibilityCullingForDynamicBounds(
	TEXT("fx.Niagara.AllowVisibilityCullingForDynamicBounds"),
	GbAllowVisibilityCullingForDynamicBounds,
	TEXT("Allow async work to continue until the end of the frame, if false it will complete within the tick group it's started in."),
	ECVF_Default
);

FAutoConsoleCommandWithWorld DumpNiagaraWorldManagerCommand(
	TEXT("DumpNiagaraWorldManager"),
	TEXT("Dump Information About the Niagara World Manager Contents"),
	FConsoleCommandWithWorldDelegate::CreateLambda(
		[](UWorld* World)
		{
			FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World);
			if (WorldManager != nullptr && GLog != nullptr)
			{
				WorldManager->DumpDetails(*GLog);
			}
		}
	)
);

static int GEnableNiagaraVisCulling = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraVisCulling(
	TEXT("fx.Niagara.Scalability.VisibilityCulling"),
	GEnableNiagaraVisCulling,
	TEXT("When non-zero, high level scalability culling based on visibility is enabled."),
	ECVF_Default
);

static int GEnableNiagaraDistanceCulling = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraDistanceCulling(
	TEXT("fx.Niagara.Scalability.DistanceCulling"),
	GEnableNiagaraDistanceCulling,
	TEXT("When non-zero, high level scalability culling based on distance is enabled."),
	ECVF_Default
);

static int GEnableNiagaraInstanceCountCulling = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraInstanceCountCulling(
	TEXT("fx.Niagara.Scalability.InstanceCountCulling"),
	GEnableNiagaraInstanceCountCulling,
	TEXT("When non-zero, high level scalability culling based on instance count is enabled."),
	ECVF_Default
);

static int GEnableNiagaraGlobalBudgetCulling = 1;
static FAutoConsoleVariableRef CVarEnableNiagaraGlobalBudgetCulling(
	TEXT("fx.Niagara.Scalability.GlobalBudgetCulling"),
	GEnableNiagaraGlobalBudgetCulling,
	TEXT("When non-zero, high level scalability culling based on global time budget is enabled."),
	ECVF_Default
);

float GWorldLoopTime = 0.0f;
static FAutoConsoleVariableRef CVarWorldLoopTime(
	TEXT("fx.Niagara.Debug.GlobalLoopTime"),
	GWorldLoopTime,
	TEXT("If > 0 all Niagara FX will reset every N seconds. \n"),
	ECVF_Default
);

FAutoConsoleCommandWithWorldAndArgs GCmdNiagaraPlaybackMode(
	TEXT("fx.Niagara.Debug.PlaybackMode"),
	TEXT("Set playback mode\n")
	TEXT("0 - Play\n")
	TEXT("1 - Paused\n")
	TEXT("2 - Step\n"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if ( FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World) )
			{
				if (Args.Num() != 1)
				{
					UE_LOG(LogNiagara, Log, TEXT("fx.Niagara.Debug.PlaybackMode %d"), (int32)WorldManager->GetDebugPlaybackMode());
				}
				else
				{
					const ENiagaraDebugPlaybackMode PlaybackMode = FMath::Clamp((ENiagaraDebugPlaybackMode)FCString::Atoi(*Args[0]), ENiagaraDebugPlaybackMode::Play, ENiagaraDebugPlaybackMode::Step);
					WorldManager->SetDebugPlaybackMode(PlaybackMode);
				}
			}
		}
	)
);

FAutoConsoleCommandWithWorldAndArgs GCmdNiagaraPlaybackRate(
	TEXT("fx.Niagara.Debug.PlaybackRate"),
	TEXT("Set playback rate\n"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if ( FNiagaraWorldManager* WorldManager = FNiagaraWorldManager::Get(World) )
			{
				if (Args.Num() != 1)
				{
					UE_LOG(LogNiagara, Log, TEXT("fx.Niagara.Debug.PlaybackRate %5.2f"), (int32)WorldManager->GetDebugPlaybackRate());
				}
				else
				{
					const float PlaybackRate = FCString::Atof(*Args[0]);
					WorldManager->SetDebugPlaybackRate(PlaybackRate);
				}
			}
		}
	)
);

FDelegateHandle FNiagaraWorldManager::OnWorldInitHandle;
FDelegateHandle FNiagaraWorldManager::OnWorldCleanupHandle;
FDelegateHandle FNiagaraWorldManager::OnPostWorldCleanupHandle;
FDelegateHandle FNiagaraWorldManager::OnPreWorldFinishDestroyHandle;
FDelegateHandle FNiagaraWorldManager::OnWorldBeginTearDownHandle;
FDelegateHandle FNiagaraWorldManager::TickWorldHandle;
FDelegateHandle FNiagaraWorldManager::OnWorldPreSendAllEndOfFrameUpdatesHandle;
FDelegateHandle FNiagaraWorldManager::PreGCHandle;
FDelegateHandle FNiagaraWorldManager::PostReachabilityAnalysisHandle;
FDelegateHandle FNiagaraWorldManager::PostGCHandle;
FDelegateHandle FNiagaraWorldManager::PreGCBeginDestroyHandle; 
TMap<class UWorld*, class FNiagaraWorldManager*> FNiagaraWorldManager::WorldManagers;

TGlobalResource<FNiagaraViewDataMgr> GNiagaraViewDataManager;

namespace FNiagaraUtilities
{
	int GetNiagaraTickGroup(ETickingGroup TickGroup)
	{
		const int ActualTickGroup = FMath::Clamp(TickGroup - NiagaraFirstTickGroup, 0, NiagaraNumTickGroups - 1);
		return ActualTickGroup;
	}
}

FNiagaraViewDataMgr::FNiagaraViewDataMgr()
	: FRenderResource()
	, SceneDepthTexture(nullptr)
	, SceneNormalTexture(nullptr)
	, ViewUniformBuffer(nullptr)
{

}

void FNiagaraViewDataMgr::Init()
{
	IRendererModule& RendererModule = GetRendererModule();

	GNiagaraViewDataManager.PostOpaqueDelegate.BindRaw(&GNiagaraViewDataManager, &FNiagaraViewDataMgr::PostOpaqueRender);
	GNiagaraViewDataManager.PostOpaqueDelegateHandle = RendererModule.RegisterPostOpaqueRenderDelegate(GNiagaraViewDataManager.PostOpaqueDelegate);
}

void FNiagaraViewDataMgr::Shutdown()
{
	IRendererModule& RendererModule = GetRendererModule();

	RendererModule.RemovePostOpaqueRenderDelegate(GNiagaraViewDataManager.PostOpaqueDelegateHandle);
	GNiagaraViewDataManager.ReleaseDynamicRHI();
}

void FNiagaraViewDataMgr::InitDynamicRHI()
{

}

void FNiagaraViewDataMgr::ReleaseDynamicRHI()
{
	SceneDepthTexture = nullptr;
	SceneNormalTexture = nullptr;
	ViewUniformBuffer = nullptr;
	SceneTexturesUniformParams.SafeRelease();
}

//////////////////////////////////////////////////////////////////////////

void FNiagaraWorldManagerTickFunction::ExecuteTick(float DeltaTime, enum ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	check(Owner);
	Owner->Tick(TickGroup, DeltaTime, TickType, CurrentThread, MyCompletionGraphEvent);
}

FString FNiagaraWorldManagerTickFunction::DiagnosticMessage()
{
	static const UEnum* EnumType = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("ETickingGroup"));

	return TEXT("FParticleSystemManager::Tick(") + EnumType->GetNameStringByIndex(static_cast<uint32>(TickGroup)) + TEXT(")");
}

FName FNiagaraWorldManagerTickFunction::DiagnosticContext(bool bDetailed)
{
	return FName(TEXT("ParticleSystemManager"));
}

//////////////////////////////////////////////////////////////////////////

FNiagaraWorldManager::FNiagaraWorldManager()
	: World(nullptr)
	, CachedEffectsQuality(INDEX_NONE)
	, bAppHasFocus(true)
{
}

void FNiagaraWorldManager::Init(UWorld* InWorld)
{
	World = InWorld;
	for (int32 TickGroup = 0; TickGroup < NiagaraNumTickGroups; ++TickGroup)
	{
		FNiagaraWorldManagerTickFunction& TickFunc = TickFunctions[TickGroup];
		TickFunc.TickGroup = ETickingGroup(NiagaraFirstTickGroup + TickGroup);
		TickFunc.EndTickGroup = GNiagaraAllowAsyncWorkToEndOfFrame ? TG_LastDemotable : (ETickingGroup)TickFunc.TickGroup;
		TickFunc.bCanEverTick = true;
		TickFunc.bStartWithTickEnabled = true;
		TickFunc.bAllowTickOnDedicatedServer = false;
		TickFunc.bHighPriority = true;
		TickFunc.Owner = this;
		TickFunc.RegisterTickFunction(InWorld->PersistentLevel);
	}

	ComponentPool = NewObject<UNiagaraComponentPool>();

	//Ideally we'd do this here but it's too early in the init process and the world does not have a Scene yet.
	//Possibly a later hook we can use.
	//PrimePoolForAllSystems();

#if !UE_BUILD_SHIPPING
	NiagaraDebugHud.Reset(new FNiagaraDebugHud(World));
#endif
}

FNiagaraWorldManager::~FNiagaraWorldManager()
{
	OnWorldCleanup(true, true);
}

FNiagaraWorldManager* FNiagaraWorldManager::Get(const UWorld* World)
{
	FNiagaraWorldManager** OutWorld = WorldManagers.Find(World);
	if (OutWorld == nullptr)
	{
		UE_LOG(LogNiagara, Warning, TEXT("Calling FNiagaraWorldManager::Get \"%s\", but Niagara has never encountered this world before. "
			" This means that WorldInit never happened. This may happen in some edge cases in the editor, like saving invisible child levels, "
			"in which case the calling context needs to be safe against this returning nullptr."), World ? *World->GetName() : TEXT("nullptr"));
		return nullptr;
	}
	return *OutWorld;
}

void FNiagaraWorldManager::OnStartup()
{
	OnWorldInitHandle = FWorldDelegates::OnPreWorldInitialization.AddStatic(&FNiagaraWorldManager::OnWorldInit);
	OnWorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&FNiagaraWorldManager::OnWorldCleanup);
	OnPostWorldCleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddStatic(&FNiagaraWorldManager::OnPostWorldCleanup);
	OnPreWorldFinishDestroyHandle = FWorldDelegates::OnPreWorldFinishDestroy.AddStatic(&FNiagaraWorldManager::OnPreWorldFinishDestroy);
	OnWorldBeginTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddStatic(&FNiagaraWorldManager::OnWorldBeginTearDown);
	TickWorldHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&FNiagaraWorldManager::TickWorld);
	OnWorldPreSendAllEndOfFrameUpdatesHandle = FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddLambda(
		[](UWorld* InWorld)
		{
			if ( FNiagaraWorldManager* FoundManager = WorldManagers.FindRef(InWorld) )
			{
				FoundManager->PreSendAllEndOfFrameUpdates();
			}
		}
	);

	PreGCHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(&FNiagaraWorldManager::OnPreGarbageCollect);
	PostReachabilityAnalysisHandle = FCoreUObjectDelegates::PostReachabilityAnalysis.AddStatic(&FNiagaraWorldManager::OnPostReachabilityAnalysis);
	PostGCHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddStatic(&FNiagaraWorldManager::OnPostGarbageCollect);
	PreGCBeginDestroyHandle = FCoreUObjectDelegates::PreGarbageCollectConditionalBeginDestroy.AddStatic(&FNiagaraWorldManager::OnPreGarbageCollectBeginDestroy);
}

void FNiagaraWorldManager::OnShutdown()
{
	FWorldDelegates::OnPreWorldInitialization.Remove(OnWorldInitHandle);
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
	FWorldDelegates::OnPostWorldCleanup.Remove(OnPostWorldCleanupHandle);
	FWorldDelegates::OnPreWorldFinishDestroy.Remove(OnPreWorldFinishDestroyHandle);
	FWorldDelegates::OnWorldBeginTearDown.Remove(OnWorldBeginTearDownHandle);
	FWorldDelegates::OnWorldPostActorTick.Remove(TickWorldHandle);
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.Remove(OnWorldPreSendAllEndOfFrameUpdatesHandle);

	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(PreGCHandle);
	FCoreUObjectDelegates::PostReachabilityAnalysis.Remove(PostReachabilityAnalysisHandle);
	FCoreUObjectDelegates::GetPostGarbageCollect().Remove(PostGCHandle);
	FCoreUObjectDelegates::PreGarbageCollectConditionalBeginDestroy.Remove(PreGCBeginDestroyHandle);


	//Should have cleared up all world managers by now.
	check(WorldManagers.Num() == 0);
	for (TPair<UWorld*, FNiagaraWorldManager*> Pair : WorldManagers)
	{
		delete Pair.Value;
		Pair.Value = nullptr;
	}
}

void FNiagaraWorldManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	// World doesn't need to be added to the reference list. It will be handled via OnWorldInit & OnWorldCleanup & OnPreWorldFinishDestroy in INiagaraModule

	Collector.AddReferencedObjects(ParameterCollections);
	Collector.AddReferencedObject(ComponentPool);
	for (auto& Pair : ScalabilityManagers)
	{
		UNiagaraEffectType* EffectType = Pair.Key;
		FNiagaraScalabilityManager& ScalabilityMan = Pair.Value;

		Collector.AddReferencedObject(EffectType);
		ScalabilityMan.AddReferencedObjects(Collector);
	}
}

FString FNiagaraWorldManager::GetReferencerName() const
{
	return TEXT("FNiagaraWorldManager");
}

UNiagaraParameterCollectionInstance* FNiagaraWorldManager::GetParameterCollection(UNiagaraParameterCollection* Collection)
{
	if (!Collection)
	{
		return nullptr;
	}

	UNiagaraParameterCollectionInstance** OverrideInst = ParameterCollections.Find(Collection);
	if (!OverrideInst)
	{
		UNiagaraParameterCollectionInstance* DefaultInstance = Collection->GetDefaultInstance();
		OverrideInst = &ParameterCollections.Add(Collection);
		*OverrideInst = CastChecked<UNiagaraParameterCollectionInstance>(StaticDuplicateObject(DefaultInstance, World));
#if WITH_EDITORONLY_DATA
		//Bind to the default instance so that changes to the collection propagate through.
		DefaultInstance->GetParameterStore().Bind(&(*OverrideInst)->GetParameterStore());
#endif

		(*OverrideInst)->Bind(World);
	}

	check(OverrideInst && *OverrideInst);
	return *OverrideInst;
}

void FNiagaraWorldManager::CleanupParameterCollections()
{
#if WITH_EDITOR
	for (TPair<UNiagaraParameterCollection*, UNiagaraParameterCollectionInstance*> CollectionInstPair : ParameterCollections)
	{
		UNiagaraParameterCollection* Collection = CollectionInstPair.Key;
		UNiagaraParameterCollectionInstance* CollectionInst = CollectionInstPair.Value;
		//Ensure that the default instance is not bound to the override.
		UNiagaraParameterCollectionInstance* DefaultInst = Collection->GetDefaultInstance();
		DefaultInst->GetParameterStore().Unbind(&CollectionInst->GetParameterStore());
	}
#endif
	ParameterCollections.Empty();
}

TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe> FNiagaraWorldManager::GetSystemSimulation(ETickingGroup TickGroup, UNiagaraSystem* System)
{
	LLM_SCOPE(ELLMTag::Niagara);

	const int32 ActualTickGroup = FNiagaraUtilities::GetNiagaraTickGroup(TickGroup);

	TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>* SimPtr = SystemSimulations[ActualTickGroup].Find(System);
	if (SimPtr != nullptr)
	{
		return *SimPtr;
	}
	
	TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe> Sim = MakeShared<FNiagaraSystemSimulation, ESPMode::ThreadSafe>();
	SystemSimulations[ActualTickGroup].Add(System, Sim);
	Sim->Init(System, World, false, TickGroup);
	return Sim;
}

void FNiagaraWorldManager::DestroySystemSimulation(UNiagaraSystem* System)
{
	for ( int TG=0; TG < NiagaraNumTickGroups; ++TG )
	{
		TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>* Simulation = SystemSimulations[TG].Find(System);
		if (Simulation != nullptr)
		{
			(*Simulation)->Destroy();
			SystemSimulations[TG].Remove(System);
		}
	}
	ComponentPool->RemoveComponentsBySystem(System);
}

void FNiagaraWorldManager::DestroySystemInstance(TUniquePtr<FNiagaraSystemInstance>& InPtr)
{
	check(IsInGameThread());
	check(InPtr != nullptr);
	DeferredDeletionQueue.Emplace(MoveTemp(InPtr));
}

void FNiagaraWorldManager::OnBatcherDestroyed_Internal(NiagaraEmitterInstanceBatcher* InBatcher)
{
	// Process the deferred deletion queue before deleting the batcher of this world.
	// This is required because the batcher is accessed in FNiagaraEmitterInstance::~FNiagaraEmitterInstance
	if (World && World->FXSystem && World->FXSystem->GetInterface(NiagaraEmitterInstanceBatcher::Name) == InBatcher)
	{
		DeferredDeletionQueue.Empty();
	}
}

void FNiagaraWorldManager::OnWorldCleanup(bool bSessionEnded, bool bCleanupResources)
{
	ComponentPool->Cleanup(World);

	for (int TG = 0; TG < NiagaraNumTickGroups; ++TG)
	{
		for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SimPair : SystemSimulations[TG])
		{
			SimPair.Value->Destroy();
		}
		SystemSimulations[TG].Empty();
	}
	CleanupParameterCollections();

	DeferredDeletionQueue.Empty();

	ScalabilityManagers.Empty();
}

void FNiagaraWorldManager::OnPostWorldCleanup(bool bSessionEnded, bool bCleanupResources)
{
	ComponentPool->Cleanup(World);
}

void FNiagaraWorldManager::PreGarbageCollect()
{
	if (GNiagaraWaitOnPreGC)
	{
		SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManWaitPreGC);
		// We must wait for system simulation & instance async ticks to complete before garbage collection can start
		// The reason for this is that our async ticks could be referencing GC objects, i.e. skel meshes, etc, and we don't want them to become unreachable while we are potentially using them
		for (int TG = 0; TG < NiagaraNumTickGroups; ++TG)
		{
			for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SimPair : SystemSimulations[TG])
			{
				SimPair.Value->WaitForInstancesTickComplete();
			}
		}
	}
}

void FNiagaraWorldManager::PostReachabilityAnalysis()
{
	for (TObjectIterator<UNiagaraComponent> ComponentIt; ComponentIt; ++ComponentIt)
	{
		ComponentIt->GetOverrideParameters().MarkUObjectsDirty();
	}
}


void FNiagaraWorldManager::PostGarbageCollect()
{
	//Clear out and scalability managers who's EffectTypes have been GCd.
	while (ScalabilityManagers.Remove(nullptr)) {}
}

void FNiagaraWorldManager::PreGarbageCollectBeginDestroy()
{
	//Clear out and scalability managers who's EffectTypes have been GCd.
	while (ScalabilityManagers.Remove(nullptr)) {}

	//Also tell the scalability managers to clear out any references the GC has nulled.
	for (auto& Pair : ScalabilityManagers)
	{
		FNiagaraScalabilityManager& ScalabilityMan = Pair.Value;
		UNiagaraEffectType* EffectType = Pair.Key;
		ScalabilityMan.PreGarbageCollectBeginDestroy();
	}
}

void FNiagaraWorldManager::OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS)
{
	check(WorldManagers.Find(World) == nullptr);
	FNiagaraWorldManager*& NewManager = WorldManagers.Add(World);
	NewManager = new FNiagaraWorldManager();
	NewManager->Init(World);
}

void FNiagaraWorldManager::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	//Cleanup world manager contents but not the manager itself.
	FNiagaraWorldManager** Manager = WorldManagers.Find(World);
	if (Manager)
	{
		(*Manager)->OnWorldCleanup(bSessionEnded, bCleanupResources);
	}
}

void FNiagaraWorldManager::OnPostWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	//Cleanup world manager contents but not the manager itself.
	FNiagaraWorldManager** Manager = WorldManagers.Find(World);
	if (Manager)
	{
		(*Manager)->OnPostWorldCleanup(bSessionEnded, bCleanupResources);
	}
}

void FNiagaraWorldManager::OnPreWorldFinishDestroy(UWorld* World)
{
	FNiagaraWorldManager** Manager = WorldManagers.Find(World);
	if (Manager)
	{
		delete (*Manager);
		WorldManagers.Remove(World);
	}
}

void FNiagaraWorldManager::OnWorldBeginTearDown(UWorld* World)
{
// 	FNiagaraWorldManager** Manager = WorldManagers.Find(World);
// 	if (Manager)
// 	{
// 		delete (*Manager);
// 		WorldManagers.Remove(World);
// 	}
// 	FNiagaraWorldManager** Manager = WorldManagers.Find(World);
// 	if (Manager)
// 	{
// 		Manager->SystemSimulations
// 	}
}

void FNiagaraWorldManager::OnBatcherDestroyed(NiagaraEmitterInstanceBatcher* InBatcher)
{
	for (TPair<UWorld*, FNiagaraWorldManager*>& Pair : WorldManagers)
	{
		Pair.Value->OnBatcherDestroyed_Internal(InBatcher);
	}
}

void FNiagaraWorldManager::DestroyAllSystemSimulations(class UNiagaraSystem* System)
{
	for (TPair<UWorld*, FNiagaraWorldManager*>& Pair : WorldManagers)
	{
		Pair.Value->DestroySystemSimulation(System);
	}
}

void FNiagaraWorldManager::TickWorld(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	Get(World)->PostActorTick(DeltaSeconds);
}

void FNiagaraWorldManager::OnPreGarbageCollect()
{
	for (TPair<UWorld*, FNiagaraWorldManager*>& Pair : WorldManagers)
	{
		Pair.Value->PreGarbageCollect();
	}
}

void FNiagaraWorldManager::OnPostReachabilityAnalysis()
{
	for (TPair<UWorld*, FNiagaraWorldManager*>& Pair : WorldManagers)
	{
		Pair.Value->PostReachabilityAnalysis();
	}
}

void FNiagaraWorldManager::OnPostGarbageCollect()
{
	for (TPair<UWorld*, FNiagaraWorldManager*>& Pair : WorldManagers)
	{
		Pair.Value->PostGarbageCollect();
	}
}

void FNiagaraWorldManager::OnPreGarbageCollectBeginDestroy()
{
	for (TPair<UWorld*, FNiagaraWorldManager*>& Pair : WorldManagers)
	{
		Pair.Value->PreGarbageCollectBeginDestroy();
	}
}

void FNiagaraWorldManager::PostActorTick(float DeltaSeconds)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManTick);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_NiagaraPostActorTick_GT);

	DeltaSeconds *= DebugPlaybackRate;

	// Update any systems with post actor work
	// - Instances that need to move to a higher tick group
	// - Instances that are pending spawn
	// - Instances that were spawned and we need to ensure the async tick is complete
	if (SimulationsWithPostActorWork.Num() > 0)
	{
		for (int32 i=0; i < SimulationsWithPostActorWork.Num(); ++i )
		{
			if (!SimulationsWithPostActorWork[i]->IsValid())
			{
				SimulationsWithPostActorWork.RemoveAtSwap(i, 1, false);
				--i;
			}
			else
			{
				SimulationsWithPostActorWork[i]->WaitForInstancesTickComplete();
			}
		}

		for (int32 i=0; i < SimulationsWithPostActorWork.Num(); ++i)
		{
			if (!SimulationsWithPostActorWork[i]->IsValid())
			{
				SimulationsWithPostActorWork.RemoveAtSwap(i, 1, false);
				--i;
			}
			else
			{
				SimulationsWithPostActorWork[i]->UpdateTickGroups_GameThread();
			}
		}

		for (const auto& Simulation : SimulationsWithPostActorWork)
		{
			if (Simulation->IsValid())
			{
				Simulation->Spawn_GameThread(DeltaSeconds, true);
			}
		}

		SimulationsWithPostActorWork.Reset();
	}

	// Clear cached player view location list, it should never be used outside of the world tick
	bCachedPlayerViewLocationsValid = false;
	CachedPlayerViewLocations.Reset();

	// Delete any instances that were pending deletion
	//-TODO: This could be done after each system sim has run
	DeferredDeletionQueue.Empty();

	// Update tick groups
	for (FNiagaraWorldManagerTickFunction& TickFunc : TickFunctions )
	{
		TickFunc.EndTickGroup = GNiagaraAllowAsyncWorkToEndOfFrame ? TG_LastDemotable : (ETickingGroup)TickFunc.TickGroup;
	}

	// Tick debug HUD for the world
	if (NiagaraDebugHud != nullptr)
	{
		NiagaraDebugHud->GatherSystemInfo();
	}

	if ( DebugPlaybackMode == ENiagaraDebugPlaybackMode::Step )
	{
		RequestedDebugPlaybackMode = ENiagaraDebugPlaybackMode::Paused;
		DebugPlaybackMode = ENiagaraDebugPlaybackMode::Paused;
	}
}

void FNiagaraWorldManager::PreSendAllEndOfFrameUpdates()
{
	for (const auto& Simulation : SimulationsWithPostActorWork)
	{
		if (Simulation->IsValid())
		{
			Simulation->WaitForInstancesTickComplete();
		}
	}
	SimulationsWithPostActorWork.Reset();
}

void FNiagaraWorldManager::MarkSimulationForPostActorWork(FNiagaraSystemSimulation* SystemSimulation)
{
	check(SystemSimulation != nullptr);
	if ( !SimulationsWithPostActorWork.ContainsByPredicate([&](const TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>& Existing) { return &Existing.Get() == SystemSimulation; }) )
	{
		SimulationsWithPostActorWork.Add(SystemSimulation->AsShared());
	}
}

void FNiagaraWorldManager::Tick(ETickingGroup TickGroup, float DeltaSeconds, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	check(TickGroup >= NiagaraFirstTickGroup && TickGroup <= NiagaraLastTickGroup);

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Effects);
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManTick);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);

	DeltaSeconds *= DebugPlaybackRate;

	// We do book keeping in the first tick group
	if ( TickGroup == NiagaraFirstTickGroup )
	{		
		// Update playback mode
		DebugPlaybackMode = RequestedDebugPlaybackMode;

		// Utility loop feature to trigger all systems to loop on a timer.
		if (GWorldLoopTime > 0.0f)
		{
			if (WorldLoopTime <= 0.0f)
			{
				WorldLoopTime = GWorldLoopTime;
				for (TObjectIterator<UNiagaraComponent> It; It; ++It)
				{
					if (UNiagaraComponent* Comp = *It)
					{
						if(Comp->GetWorld() == GetWorld())
						{
							Comp->ResetSystem();
						}
					}
				}
			}
			WorldLoopTime -= DeltaSeconds;
		}

		//Ensure the pools have been primed.
		//WorldInit is too early.
		if(!bPoolIsPrimed)
		{
			PrimePoolForAllSystems();
			bPoolIsPrimed = true;
		}

		FNiagaraSharedObject::FlushDeletionList();

#if WITH_EDITOR //PLATFORM_DESKTOP
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_Niagara_IsThisApplicationForeground);
			bAppHasFocus = FPlatformApplicationMisc::IsThisApplicationForeground();
		}
#else
		bAppHasFocus = true;
#endif

		// If we are in paused don't do anything
		if (DebugPlaybackMode == ENiagaraDebugPlaybackMode::Paused)
		{
			return;
		}

		// Cache player view locations for all system instances to access
		//-TODO: Do we need to do this per tick group?
		bCachedPlayerViewLocationsValid = true;
		if (World->GetPlayerControllerIterator())
		{
			for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
			{
				APlayerController* PlayerController = Iterator->Get();
				if (PlayerController && PlayerController->IsLocalPlayerController())
				{
					FVector* POVLoc = new(CachedPlayerViewLocations) FVector;
					FRotator POVRotation;
					PlayerController->GetPlayerViewPoint(*POVLoc, POVRotation);
				}
			}
		}
		else
		{
			CachedPlayerViewLocations.Append(World->ViewLocationsRenderedLastFrame);
		}

#if WITH_EDITORONLY_DATA
		if (GCurrentLevelEditingViewportClient && (GCurrentLevelEditingViewportClient->GetWorld() == World))
		{
			const FViewportCameraTransform& ViewTransform = GCurrentLevelEditingViewportClient->GetViewTransform();
			CachedPlayerViewLocations.Add(ViewTransform.GetLocation());
		}
#endif

		UpdateScalabilityManagers(DeltaSeconds, false);

		//Tick our collections to push any changes to bound stores.
		//-TODO: Do we need to do this per tick group?
		for (TPair<UNiagaraParameterCollection*, UNiagaraParameterCollectionInstance*> CollectionInstPair : ParameterCollections)
		{
			check(CollectionInstPair.Value);
			CollectionInstPair.Value->Tick(World);
		}
	}

	// If we are in paused don't do anything
	if ( DebugPlaybackMode == ENiagaraDebugPlaybackMode::Paused )
	{
		return;
	}

	// Tick generated data
	for (auto& GeneratedData : DIGeneratedData)
	{
		GeneratedData.Value->Tick(TickGroup, DeltaSeconds);
	}

	// Now tick all system instances. 
	const int ActualTickGroup = FNiagaraUtilities::GetNiagaraTickGroup(TickGroup);

	TArray<UNiagaraSystem*, TInlineAllocator<4>> DeadSystems;
	for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SystemSim : SystemSimulations[ActualTickGroup])
	{
		FNiagaraSystemSimulation*  Sim = &SystemSim.Value.Get();

		if (Sim->IsValid())
		{
			Sim->Tick_GameThread(DeltaSeconds, MyCompletionGraphEvent);
		}
		else
		{
			DeadSystems.Add(SystemSim.Key);
		}
	}

	for (UNiagaraSystem* DeadSystem : DeadSystems)
	{
		SystemSimulations[ActualTickGroup].Remove(DeadSystem);
	}

	// Loop over all simulations that have been marked for post actor (i.e. ones whos TG is changing or have pending spawn systems)
	if (GNiagaraSpawnPerTickGroup && (SimulationsWithPostActorWork.Num() > 0))
	{
		//We update scalability managers here so that any new systems can be culled or setup with other scalability based parameters correctly for their spawn.
		UpdateScalabilityManagers(DeltaSeconds, true);

		QUICK_SCOPE_CYCLE_COUNTER(STAT_NiagaraSpawnPerTickGroup_GT);
		for (int32 i = 0; i < SimulationsWithPostActorWork.Num(); ++i)
		{
			const auto& Simulation = SimulationsWithPostActorWork[i];
			if (Simulation->IsValid() && (Simulation->GetTickGroup() < TickGroup))
			{
				Simulation->Spawn_GameThread(DeltaSeconds, false);
			}
		}
	}
}

void FNiagaraWorldManager::DumpDetails(FOutputDevice& Ar)
{
	Ar.Logf(TEXT("=== FNiagaraWorldManager Dumping Detailed Information"));

	static const UEnum* TickingGroupEnum = FindObjectChecked<UEnum>(ANY_PACKAGE, TEXT("ETickingGroup"));

	for ( int TG=0; TG < NiagaraNumTickGroups; ++TG )
	{
		if (SystemSimulations[TG].Num() == 0 )
		{
			continue;
		}

		Ar.Logf(TEXT("TickingGroup %s"), *TickingGroupEnum->GetNameStringByIndex(TG + NiagaraFirstTickGroup));

		for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SystemSim : SystemSimulations[TG])
		{
			FNiagaraSystemSimulation* Sim = &SystemSim.Value.Get();
			if ( !Sim->IsValid() )
			{
				continue;
			}

			Ar.Logf(TEXT("\tSimulation %s"), *Sim->GetSystem()->GetFullName());
			Sim->DumpTickInfo(Ar);
		}
	}
}

UWorld* FNiagaraWorldManager::GetWorld()
{
	return World;
}

//////////////////////////////////////////////////////////////////////////

void FNiagaraWorldManager::UpdateScalabilityManagers(float DeltaSeconds, bool bNewSpawnsOnly)
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateScalabilityManagers);

	for (auto& Pair : ScalabilityManagers)
	{
		FNiagaraScalabilityManager& ScalabilityMan = Pair.Value;
		UNiagaraEffectType* EffectType = Pair.Key;
		check(EffectType);

		if (bNewSpawnsOnly)
		{
			ScalabilityMan.Update(this, DeltaSeconds, true);
		}
		else
		{
			ScalabilityMan.Update(this, DeltaSeconds, false);
		}
	}
}

void FNiagaraWorldManager::RegisterWithScalabilityManager(UNiagaraComponent* Component)
{
	check(Component);
	if (UNiagaraEffectType* EffectType = Component->GetAsset()->GetEffectType())
	{
		FNiagaraScalabilityManager* ScalabilityManager = ScalabilityManagers.Find(EffectType);

		if (!ScalabilityManager)
		{
			ScalabilityManager = &ScalabilityManagers.Add(EffectType);
			ScalabilityManager->EffectType = EffectType;
		}

		ScalabilityManager->Register(Component);
	}
}

void FNiagaraWorldManager::UnregisterWithScalabilityManager(UNiagaraComponent* Component)
{
	check(Component);
	if (UNiagaraEffectType* EffectType = Component->GetAsset()->GetEffectType())
	{
		//Possibly the manager has been GCd.
		if (FNiagaraScalabilityManager* ScalabilityManager = ScalabilityManagers.Find(EffectType))
		{
			ScalabilityManager->Unregister(Component);
		}
		else
		{
			//The component does this itself in unregister.
			//Component->ScalabilityManagerHandle = INDEX_NONE;
		}
	}
}

bool FNiagaraWorldManager::ShouldPreCull(UNiagaraSystem* System, UNiagaraComponent* Component)
{
	if (System)
	{
		if (UNiagaraEffectType* EffectType = System->GetEffectType())
		{
			if (CanPreCull(EffectType))
			{
				FNiagaraScalabilityState State;
				const FNiagaraSystemScalabilitySettings& ScalabilitySettings = System->GetScalabilitySettings();
				CalculateScalabilityState(System, ScalabilitySettings, EffectType, Component, true, FFXBudget::GetWorstAdjustedUsage(), State);
				return State.bCulled;
			}
		}
	}
	return false;
}

bool FNiagaraWorldManager::ShouldPreCull(UNiagaraSystem* System, FVector Location)
{
	if (System)
	{
		if (UNiagaraEffectType* EffectType = System->GetEffectType())
		{
			if (CanPreCull(EffectType))
			{
				FNiagaraScalabilityState State;
				const FNiagaraSystemScalabilitySettings& ScalabilitySettings = System->GetScalabilitySettings();
				CalculateScalabilityState(System, ScalabilitySettings, EffectType, Location, true, FFXBudget::GetWorstAdjustedUsage(), State);
				//TODO: Tell the debugger about recently PreCulled systems.
				return State.bCulled;
			}
		}
	}
	return false;
}

void FNiagaraWorldManager::CalculateScalabilityState(UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraEffectType* EffectType, FVector Location, bool bIsPreCull, float WorstGlobalBudgetUse, FNiagaraScalabilityState& OutState)
{
	OutState.bCulled = false;

	DistanceCull(EffectType, ScalabilitySettings, Location, OutState);

	//If we have no significance handler there is no concept of relative significance for these systems so we can just pre cull if we go over the instance count.
	if (GEnableNiagaraInstanceCountCulling && bIsPreCull && EffectType->GetSignificanceHandler() == nullptr)
	{
		InstanceCountCull(EffectType, System, ScalabilitySettings, OutState);
	}

	//Cull if any of our budgets are exceeded.
 	if (FFXBudget::Enabled() && INiagaraModule::UseGlobalFXBudget() && GEnableNiagaraGlobalBudgetCulling && ScalabilitySettings.bCullByGlobalBudget)
 	{
 		GlobalBudgetCull(ScalabilitySettings, WorstGlobalBudgetUse, OutState);
 	}

	//TODO: More progressive scalability options?
}

void FNiagaraWorldManager::CalculateScalabilityState(UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraEffectType* EffectType, UNiagaraComponent* Component, bool bIsPreCull, float WorstGlobalBudgetUse, FNiagaraScalabilityState& OutState)
{
	OutState.bCulled = false;

	DistanceCull(EffectType, ScalabilitySettings, Component, OutState);
	
	if ((GbAllowVisibilityCullingForDynamicBounds || System->bFixedBounds) && bAppHasFocus && GEnableNiagaraVisCulling)
	{
		VisibilityCull(EffectType, ScalabilitySettings, Component, OutState);
	}

	//Only apply hard instance count cull limit for precull if we have no significance handler.
	if (GEnableNiagaraInstanceCountCulling && bIsPreCull && EffectType->GetSignificanceHandler() == nullptr)
	{
		InstanceCountCull(EffectType, System, ScalabilitySettings, OutState);
	}

 	if (FFXBudget::Enabled() && INiagaraModule::UseGlobalFXBudget() && GEnableNiagaraGlobalBudgetCulling && ScalabilitySettings.bCullByGlobalBudget)
	{
 		GlobalBudgetCull(ScalabilitySettings, WorstGlobalBudgetUse, OutState);
 	}

	//TODO: More progressive scalability options?

#if WITH_NIAGARA_DEBUGGER
	//Tell the debugger about our scalability state.
	//Unfortunately cannot have the debugger just read the manager state data as components are removed.
	//To avoid extra copy in future we can have the manager keep another list of recently removed component states which is cleaned up on component destruction.
	Component->DebugCachedScalabilityState = OutState;
#endif
}

bool FNiagaraWorldManager::CanPreCull(UNiagaraEffectType* EffectType)
{
	checkSlow(EffectType);
	return EffectType->CullReaction == ENiagaraCullReaction::Deactivate || EffectType->CullReaction == ENiagaraCullReaction::DeactivateImmediate;
}

void FNiagaraWorldManager::SortedSignificanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, float Significance, int32& EffectTypeInstCount, int32& SystemInstCount, FNiagaraScalabilityState& OutState)
{
	//Cull all but the N most significance FX.
	bool bCull = false;
	
	if(GEnableNiagaraInstanceCountCulling)
	{
		bCull = ScalabilitySettings.bCullMaxInstanceCount && EffectTypeInstCount >= ScalabilitySettings.MaxInstances;
		bCull |= ScalabilitySettings.bCullPerSystemMaxInstanceCount && SystemInstCount >= ScalabilitySettings.MaxSystemInstances;
	}

	OutState.bCulled |= bCull;

	//Only increment the instance counts if this is not culled. Including other causes of culling.
	if(OutState.bCulled == false)
	{
		++EffectTypeInstCount;
		++SystemInstCount;
	}

#if DEBUG_SCALABILITY_STATE
	OutState.bCulledByInstanceCount = bCull;
#endif
}

void FNiagaraWorldManager::VisibilityCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraComponent* Component, FNiagaraScalabilityState& OutState)
{
	float TimeSinceRendered = Component->GetSafeTimeSinceRendered(World->TimeSeconds);
	bool bCull = Component->GetLastRenderTime() >= 0.0f && ScalabilitySettings.bCullByMaxTimeWithoutRender && TimeSinceRendered > ScalabilitySettings.MaxTimeWithoutRender;

	OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
	OutState.bCulledByVisibility = bCull;
#endif
}

void FNiagaraWorldManager::InstanceCountCull(UNiagaraEffectType* EffectType, UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, FNiagaraScalabilityState& OutState)
{
	bool bCull = ScalabilitySettings.bCullMaxInstanceCount && EffectType->NumInstances >= ScalabilitySettings.MaxInstances;
	bCull |= ScalabilitySettings.bCullPerSystemMaxInstanceCount && System->GetActiveInstancesCount() >= ScalabilitySettings.MaxSystemInstances;
	OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
	OutState.bCulledByInstanceCount = bCull;
#endif
}

void FNiagaraWorldManager::DistanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraComponent* Component, FNiagaraScalabilityState& OutState)
{
	float LODDistance = 0.0f;

	//Directly drive the system lod distance from here.
	float MaxDist = ScalabilitySettings.MaxDistance;
#if WITH_NIAGARA_COMPONENT_PREVIEW_DATA
	if (Component->bEnablePreviewLODDistance)
	{
		LODDistance = Component->PreviewLODDistance;
	}
	else
#endif
	if(bCachedPlayerViewLocationsValid)
	{
		float ClosestDistSq = FLT_MAX;
		FVector Location = Component->GetComponentLocation();
		for (FVector ViewLocation : CachedPlayerViewLocations)
		{
			ClosestDistSq = FMath::Min(ClosestDistSq, FVector::DistSquared(ViewLocation, Location));
		}

		LODDistance = FMath::Sqrt(ClosestDistSq);
	}

	Component->SetLODDistance(LODDistance, FMath::Max(MaxDist, 1.0f));

	if (GEnableNiagaraDistanceCulling && ScalabilitySettings.bCullByDistance)
	{
		bool bCull = LODDistance > ScalabilitySettings.MaxDistance;
		OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
		OutState.bCulledByDistance = bCull;
#endif
	}
}

void FNiagaraWorldManager::DistanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, FVector Location, FNiagaraScalabilityState& OutState)
{
	if (bCachedPlayerViewLocationsValid)
	{
		float ClosestDistSq = FLT_MAX;
		for (FVector ViewLocation : CachedPlayerViewLocations)
		{
			ClosestDistSq = FMath::Min(ClosestDistSq, FVector::DistSquared(ViewLocation, Location));
		}

		if (GEnableNiagaraDistanceCulling && ScalabilitySettings.bCullByDistance)
		{
			float ClosestDist = FMath::Sqrt(ClosestDistSq);
			bool bCull = ClosestDist > ScalabilitySettings.MaxDistance;
			OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
			OutState.bCulledByDistance = bCull;
#endif
		}
	}
}

void FNiagaraWorldManager::GlobalBudgetCull(const FNiagaraSystemScalabilitySettings& ScalabilitySettings, float WorstGlobalBudgetUse, FNiagaraScalabilityState& OutState)
{
 	bool bCull = WorstGlobalBudgetUse >= ScalabilitySettings.MaxGlobalBudgetUsage;
 	OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
	OutState.bCulledByGlobalBudget = bCull;
#endif
}

bool FNiagaraWorldManager::GetScalabilityState(UNiagaraComponent* Component, FNiagaraScalabilityState& OutState) const
{
	if ( Component )
	{
		const int32 ScalabilityHandle = Component->GetScalabilityManagerHandle();
		if (ScalabilityHandle != INDEX_NONE)
		{
			if (UNiagaraSystem* System = Component->GetAsset())
			{
				if (UNiagaraEffectType* EffectType = System->GetEffectType())
				{
					if (const FNiagaraScalabilityManager* ScalabilityManager = ScalabilityManagers.Find(EffectType))
					{
						OutState = ScalabilityManager->State[ScalabilityHandle];
						return true;
					}
				}
			}
		}
	}
	return false;
}

void FNiagaraWorldManager::PrimePoolForAllWorlds(UNiagaraSystem* System)
{
	if (GNigaraAllowPrimedPools)
	{
		for (auto& Pair : WorldManagers)
		{
			if (Pair.Value)
			{
				Pair.Value->PrimePool(System);
			}
		}
	}
}

void FNiagaraWorldManager::PrimePoolForAllSystems()
{
	if (GNigaraAllowPrimedPools && World && World->IsGameWorld() && !World->bIsTearingDown)
	{
		//Prime the pool for all currently loaded systems.
		for (TObjectIterator<UNiagaraSystem> It; It; ++It)
		{
			if (UNiagaraSystem* Sys = *It)
			{
				ComponentPool->PrimePool(Sys, World);
			}
		}
	}
}

void FNiagaraWorldManager::PrimePool(UNiagaraSystem* System)
{
	if (GNigaraAllowPrimedPools && World && World->IsGameWorld() && !World->bIsTearingDown)
	{
		ComponentPool->PrimePool(System, World);
	}
}


#if DEBUG_SCALABILITY_STATE

void FNiagaraWorldManager::DumpScalabilityState()
{
	UE_LOG(LogNiagara, Display, TEXT("========================================================================"));
	UE_LOG(LogNiagara, Display, TEXT("Niagara World Manager Scalability State. %0xP - %s"), World, *World->GetPathName());
	UE_LOG(LogNiagara, Display, TEXT("========================================================================"));

	for (auto& Pair : ScalabilityManagers)
	{
		FNiagaraScalabilityManager& ScalabilityMan = Pair.Value;
		ScalabilityMan.Dump();
	}

	UE_LOG(LogNiagara, Display, TEXT("========================================================================"));
}


FAutoConsoleCommandWithWorld GDumpNiagaraScalabilityData(
	TEXT("fx.DumpNiagaraScalabilityState"),
	TEXT("Dumps state information for all Niagara Scalability Mangers."),
	FConsoleCommandWithWorldDelegate::CreateStatic(
		[](UWorld* World)
{
	FNiagaraWorldManager* WorldMan = FNiagaraWorldManager::Get(World);
	WorldMan->DumpScalabilityState();
}));

#endif
