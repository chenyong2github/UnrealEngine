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
#include "HAL/PlatformApplicationMisc.h"

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

FDelegateHandle FNiagaraWorldManager::OnWorldInitHandle;
FDelegateHandle FNiagaraWorldManager::OnWorldCleanupHandle;
FDelegateHandle FNiagaraWorldManager::OnPreWorldFinishDestroyHandle;
FDelegateHandle FNiagaraWorldManager::OnWorldBeginTearDownHandle;
FDelegateHandle FNiagaraWorldManager::TickWorldHandle;
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
	RendererModule.RegisterPostOpaqueRenderDelegate(GNiagaraViewDataManager.PostOpaqueDelegate);
}

void FNiagaraViewDataMgr::Shutdown()
{
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

FNiagaraWorldManager::FNiagaraWorldManager(UWorld* InWorld)
	: World(InWorld)
	, CachedEffectsQuality(INDEX_NONE)
	, bAppHasFocus(true)
{
	for (int32 TickGroup=0; TickGroup < NiagaraNumTickGroups; ++TickGroup)
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
	OnPreWorldFinishDestroyHandle = FWorldDelegates::OnPreWorldFinishDestroy.AddStatic(&FNiagaraWorldManager::OnPreWorldFinishDestroy);
	OnWorldBeginTearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddStatic(&FNiagaraWorldManager::OnWorldBeginTearDown);
	TickWorldHandle = FWorldDelegates::OnWorldPostActorTick.AddStatic(&FNiagaraWorldManager::TickWorld);

	PreGCHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(&FNiagaraWorldManager::OnPreGarbageCollect);
	PostReachabilityAnalysisHandle = FCoreUObjectDelegates::PostReachabilityAnalysis.AddStatic(&FNiagaraWorldManager::OnPostReachabilityAnalysis);
	PostGCHandle = FCoreUObjectDelegates::GetPostGarbageCollect().AddStatic(&FNiagaraWorldManager::OnPostGarbageCollect);
	PreGCBeginDestroyHandle = FCoreUObjectDelegates::PreGarbageCollectConditionalBeginDestroy.AddStatic(&FNiagaraWorldManager::OnPreGarbageCollectBeginDestroy);
}

void FNiagaraWorldManager::OnShutdown()
{
	FWorldDelegates::OnPreWorldInitialization.Remove(OnWorldInitHandle);
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
	FWorldDelegates::OnPreWorldFinishDestroy.Remove(OnPreWorldFinishDestroyHandle);
	FWorldDelegates::OnWorldBeginTearDown.Remove(OnWorldBeginTearDownHandle);
	FWorldDelegates::OnWorldPostActorTick.Remove(TickWorldHandle);

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
	}

	check(OverrideInst && *OverrideInst);
	return *OverrideInst;
}

void FNiagaraWorldManager::SetParameterCollection(UNiagaraParameterCollectionInstance* NewInstance)
{
	check(NewInstance);
	if (NewInstance)
	{
		UNiagaraParameterCollection* Collection = NewInstance->GetParent();
		UNiagaraParameterCollectionInstance** OverrideInst = ParameterCollections.Find(Collection);
		if (!OverrideInst)
		{
			OverrideInst = &ParameterCollections.Add(Collection);
		}
		else
		{
			if (*OverrideInst && NewInstance)
			{
				UNiagaraParameterCollectionInstance* DefaultInstance = Collection->GetDefaultInstance();
				//Need to transfer existing bindings from old instance to new one.
				FNiagaraParameterStore& ExistingStore = (*OverrideInst)->GetParameterStore();
				FNiagaraParameterStore& NewStore = NewInstance->GetParameterStore();

				ExistingStore.TransferBindings(NewStore);

#if WITH_EDITOR
				//If the existing store was this world's duplicate of the default then we must be sure it's unbound.
				DefaultInstance->GetParameterStore().Unbind(&ExistingStore);
#endif
			}
		}

		*OverrideInst = NewInstance;
	}
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
	ComponentPool->Cleanup();

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
	WorldManagers.Add(World) = new FNiagaraWorldManager(World);
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
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManTick);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);

	// Resolve tick groups for pending spawn instances
	for (int TG=0; TG < NiagaraNumTickGroups; ++TG)
	{
		for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SystemSim : SystemSimulations[TG])
		{
			FNiagaraSystemSimulation* Sim = &SystemSim.Value.Get();
			if ( Sim->IsValid() )
			{
				Sim->UpdateTickGroups_GameThread();
			}
		}
	}

	// Execute spawn game thread
	for (int TG = 0; TG < NiagaraNumTickGroups; ++TG)
	{
		for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SystemSim : SystemSimulations[TG])
		{
			FNiagaraSystemSimulation* Sim = &SystemSim.Value.Get();
			if (Sim->IsValid())
			{
				Sim->Spawn_GameThread(DeltaSeconds);
			}
		}
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
}

void FNiagaraWorldManager::Tick(ETickingGroup TickGroup, float DeltaSeconds, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	check(TickGroup >= NiagaraFirstTickGroup && TickGroup <= NiagaraLastTickGroup);

	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManTick);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);

	// We do book keeping in the first tick group
	if ( TickGroup == NiagaraFirstTickGroup )
	{
		FNiagaraSharedObject::FlushDeletionList();

#if WITH_EDITOR //PLATFORM_DESKTOP
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_Niagara_IsThisApplicationForeground);
			bAppHasFocus = FPlatformApplicationMisc::IsThisApplicationForeground();
		}
#else
		bAppHasFocus = true;
#endif

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

		UpdateScalabilityManagers();


		//Tick our collections to push any changes to bound stores.
		//-TODO: Do we need to do this per tick group?
		for (TPair<UNiagaraParameterCollection*, UNiagaraParameterCollectionInstance*> CollectionInstPair : ParameterCollections)
		{
			check(CollectionInstPair.Value);
			CollectionInstPair.Value->Tick();
		}
	}

	// Tick skeletal mesh data
	SkeletalMeshGeneratedData.TickGeneratedData(TickGroup, DeltaSeconds);

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

void FNiagaraWorldManager::UpdateScalabilityManagers()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateScalabilityManagers);

	for (auto& Pair : ScalabilityManagers)
	{
		FNiagaraScalabilityManager& ScalabilityMan = Pair.Value;
		UNiagaraEffectType* EffectType = Pair.Key;
		check(EffectType);

		EffectType->ProcessLastFrameCycleCounts();

		//TODO: Work out how best to budget each effect type.
		//EffectType->ApplyDynamicBudget(DynamicBudget_GT, DynamicBudget_GT_CNC, DynamicBudget_RT);

		ScalabilityMan.Update(this);
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
				CalculateScalabilityState(System, ScalabilitySettings, EffectType, Component, true, State);
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
				CalculateScalabilityState(System, ScalabilitySettings, EffectType, Location, true, State);
				return State.bCulled;
			}
		}
	}
	return false;
}

void FNiagaraWorldManager::CalculateScalabilityState(UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraEffectType* EffectType, FVector Location, bool bIsPreCull, FNiagaraScalabilityState& OutState)
{
	float DistSignificance = DistanceSignificance(EffectType, ScalabilitySettings, Location);

	float Significance = DistSignificance;

	//TODO: Other significance metrics? 
	//TODO: Provide hook into game code for special case significance calcs?
	OutState.Significance = Significance;

	bool bOldCulled = OutState.bCulled;
	SignificanceCull(EffectType, ScalabilitySettings, Significance, OutState);

	//Only apply hard instance count cull limit for precull + spawn only fx. We can apply instance count via significance cull for managed fx.
	if (bIsPreCull && EffectType->UpdateFrequency == ENiagaraScalabilityUpdateFrequency::SpawnOnly)
	{
		InstanceCountCull(EffectType, ScalabilitySettings, OutState);
	}

	OutState.bDirty = OutState.bCulled != bOldCulled;

	//TODO: More progressive scalability options?
}

void FNiagaraWorldManager::CalculateScalabilityState(UNiagaraSystem* System, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraEffectType* EffectType, UNiagaraComponent* Component, bool bIsPreCull, FNiagaraScalabilityState& OutState)
{
	float DistSignificance = DistanceSignificance(EffectType, ScalabilitySettings, Component);
	
	//If/when we do have multiple drivers of significance, how best to combine them?
	float Significance = DistSignificance;

	//TODO: Other significance metrics? 
	//TODO: Provide hook into game code for special case significance calcs?
	OutState.Significance = Significance;

	bool bOldCulled = OutState.bCulled;
	OutState.bCulled = false;
	SignificanceCull(EffectType, ScalabilitySettings, Significance, OutState);
	
	//Can't cull dynamic bounds by visibility

	if (System->bFixedBounds && bAppHasFocus)
	{
		VisibilityCull(EffectType, ScalabilitySettings, Component, OutState);
	}

	//Only apply hard instance count cull limit for precull + spawn only fx. We can apply instance count via significance cull for managed fx.
	if (bIsPreCull && EffectType->UpdateFrequency == ENiagaraScalabilityUpdateFrequency::SpawnOnly)
	{
		InstanceCountCull(EffectType, ScalabilitySettings, OutState);
	}

	OutState.bDirty = OutState.bCulled != bOldCulled;

	//TODO: More progressive scalability options?
}

bool FNiagaraWorldManager::CanPreCull(UNiagaraEffectType* EffectType)
{
	checkSlow(EffectType);
	return EffectType->CullReaction == ENiagaraCullReaction::Deactivate || EffectType->CullReaction == ENiagaraCullReaction::DeactivateImmediate;
}

void FNiagaraWorldManager::SortedSignificanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, float Significance, int32 Index, FNiagaraScalabilityState& OutState)
{
	//Cull all but the N most significance FX.
	bool bCull = ScalabilitySettings.bCullMaxInstanceCount && Index >= ScalabilitySettings.MaxInstances;
	OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
	OutState.bCulledByInstanceCount = bCull;
#endif
}

void FNiagaraWorldManager::SignificanceCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, float Significance, FNiagaraScalabilityState& OutState)
{
	float MinSignificance = 0.0f;

// 	//Could We adjust the minimum significance needed by how much of this effect types budget is being used?
// 	if (ScalabilitySettings.bCullByRuntimePerf)
// 	{
// 		MinSignificance = MinSignificanceFromPerf;
// 
// 		//TODO: Other factors raising the min significance?
// 	}



	bool bCull = Significance <= MinSignificance;
	OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
	OutState.bCulledBySignificance = bCull;
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

void FNiagaraWorldManager::InstanceCountCull(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, FNiagaraScalabilityState& OutState)
{
	bool bCull = ScalabilitySettings.bCullMaxInstanceCount && EffectType->NumInstances > ScalabilitySettings.MaxInstances;
	OutState.bCulled |= bCull;
#if DEBUG_SCALABILITY_STATE
	OutState.bCulledByInstanceCount = bCull;
#endif
}

float FNiagaraWorldManager::DistanceSignificance(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, UNiagaraComponent* Component)
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

	if (ScalabilitySettings.bCullByDistance)
	{
		if (LODDistance >= ScalabilitySettings.MaxDistance)
		{
			return 0.0f;
		}

		return 1.0f - (LODDistance / ScalabilitySettings.MaxDistance);
	}
	return 1.0f;
}

float FNiagaraWorldManager::DistanceSignificance(UNiagaraEffectType* EffectType, const FNiagaraSystemScalabilitySettings& ScalabilitySettings, FVector Location)
{
	if (ScalabilitySettings.bCullByDistance && bCachedPlayerViewLocationsValid)
	{
		float ClosestDistSq = FLT_MAX;
		for (FVector ViewLocation : CachedPlayerViewLocations)
		{
			ClosestDistSq = FMath::Min(ClosestDistSq, FVector::DistSquared(ViewLocation, Location));
		}

		float ClosestDist = FMath::Sqrt(ClosestDistSq);
		if (ClosestDist >= ScalabilitySettings.MaxDistance)
		{
			return 0.0f;
		}

		return ClosestDist / ScalabilitySettings.MaxDistance;
	}
	return 1.0f;
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
