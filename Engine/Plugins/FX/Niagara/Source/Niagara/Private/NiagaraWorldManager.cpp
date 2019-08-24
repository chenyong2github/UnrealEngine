// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "GameFramework/PlayerController.h"
#include "EngineModule.h"
#include "NiagaraStats.h"
#include "NiagaraEmitterInstanceBatcher.h"
#include "NiagaraComponentPool.h"

DECLARE_CYCLE_STAT(TEXT("Niagara Manager Tick [GT]"), STAT_NiagaraWorldManTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara Manager Wait On Render [GT]"), STAT_NiagaraWorldManWaitOnRender, STATGROUP_Niagara);

FDelegateHandle FNiagaraWorldManager::OnWorldInitHandle;
FDelegateHandle FNiagaraWorldManager::OnWorldCleanupHandle;
FDelegateHandle FNiagaraWorldManager::OnPreWorldFinishDestroyHandle;
FDelegateHandle FNiagaraWorldManager::OnWorldBeginTearDownHandle;
FDelegateHandle FNiagaraWorldManager::TickWorldHandle;
TMap<class UWorld*, class FNiagaraWorldManager*> FNiagaraWorldManager::WorldManagers;

TGlobalResource<FNiagaraViewDataMgr> GNiagaraViewDataManager;

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
{
	FNiagaraWorldManagerTickFunction& TickFunc = TickFunction;//TODO: Add more tick functions to kick work earlier in frame.
	TickFunc.TickGroup = ETickingGroup::TG_LastDemotable;
	TickFunc.EndTickGroup = TickFunc.TickGroup;
	TickFunc.bCanEverTick = true;
	TickFunc.bStartWithTickEnabled = true;
	TickFunc.bHighPriority = true;
	TickFunc.Owner = this;
	TickFunc.RegisterTickFunction(InWorld->PersistentLevel);

	ComponentPool = NewObject<UNiagaraComponentPool>();
}

FNiagaraWorldManager::~FNiagaraWorldManager()
{
	OnWorldCleanup(true, true);
}

FNiagaraWorldManager* FNiagaraWorldManager::Get(UWorld* World)
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
}

void FNiagaraWorldManager::OnShutdown()
{
	FWorldDelegates::OnPreWorldInitialization.Remove(OnWorldInitHandle);
	FWorldDelegates::OnWorldCleanup.Remove(OnWorldCleanupHandle);
	FWorldDelegates::OnPreWorldFinishDestroy.Remove(OnPreWorldFinishDestroyHandle);
	FWorldDelegates::OnWorldBeginTearDown.Remove(OnWorldBeginTearDownHandle);
	FWorldDelegates::OnWorldPostActorTick.Remove(TickWorldHandle);

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

TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe> FNiagaraWorldManager::GetSystemSimulation(UNiagaraSystem* System)
{
	LLM_SCOPE(ELLMTag::Niagara);
	TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>* SimPtr = SystemSimulations.Find(System);
	if (SimPtr != nullptr)
	{
		return *SimPtr;
	}
	
	TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe> Sim = MakeShared<FNiagaraSystemSimulation, ESPMode::ThreadSafe>();
	SystemSimulations.Add(System, Sim);
	Sim->Init(System, World, false);
	return Sim;
}

void FNiagaraWorldManager::DestroySystemSimulation(UNiagaraSystem* System)
{
	TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>* Sim = SystemSimulations.Find(System);
	if (Sim)
	{
		(*Sim)->Destroy();
		SystemSimulations.Remove(System);
	}	
}

void FNiagaraWorldManager::DestroySystemInstance(TUniquePtr<FNiagaraSystemInstance>& InPtr)
{
	check(IsInGameThread());
	check(InPtr != nullptr);
	DeferredDeletionQueue[DeferredDeletionQueueIndex].Queue.Emplace(MoveTemp(InPtr));
}

void FNiagaraWorldManager::OnBatcherDestroyed_Internal(NiagaraEmitterInstanceBatcher* InBatcher)
{
	// Process the deferred deletion queue before deleting the batcher of this world.
	// This is required because the batcher is accessed in FNiagaraEmitterInstance::~FNiagaraEmitterInstance
	if (World && World->FXSystem && World->FXSystem->GetInterface(NiagaraEmitterInstanceBatcher::Name) == InBatcher)
	{
		for ( int32 i=0; i < NumDeferredQueues; ++i)
		{
			DeferredDeletionQueue[DeferredDeletionQueueIndex].Fence.Wait();
			DeferredDeletionQueue[i].Queue.Empty();
		}
	}
}


void FNiagaraWorldManager::OnWorldCleanup(bool bSessionEnded, bool bCleanupResources)
{
	ComponentPool->Cleanup();

	for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SimPair : SystemSimulations)
	{
		SimPair.Value->Destroy();
	}
	SystemSimulations.Empty();
	CleanupParameterCollections();

	for ( int32 i=0; i < NumDeferredQueues; ++i)
	{
		DeferredDeletionQueue[i].Fence.Wait();
		DeferredDeletionQueue[i].Queue.Empty();
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

void FNiagaraWorldManager::PostActorTick(float DeltaSeconds)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManTick);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);

	TArray<UNiagaraSystem*, TInlineAllocator<4>> DeadSystems;
	for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SystemSim : SystemSimulations)
	{
		FNiagaraSystemSimulation*  Sim = &SystemSim.Value.Get();
		if (Sim->IsValid())
		{
			Sim->SpawnNew_GameThread(DeltaSeconds, nullptr);
		}
		else
		{
			DeadSystems.Add(SystemSim.Key);
		}
	}
	for (UNiagaraSystem* DeadSystem : DeadSystems)
	{
		SystemSimulations.Remove(DeadSystem);
	}

	// Clear cached player view location list, it should never be used outside of the world tick
	bCachedPlayerViewLocationsValid = false;
	CachedPlayerViewLocations.Reset();

	// Enqueue fence for deferred deletion if we need to wait on anything
	if (DeferredDeletionQueue[DeferredDeletionQueueIndex].Queue.Num() > 0)
	{
		DeferredDeletionQueue[DeferredDeletionQueueIndex].Fence.BeginFence();
	}

	// Remove instances from oldest frame making sure they aren't in use on the RT
	DeferredDeletionQueueIndex = (DeferredDeletionQueueIndex + 1) % NumDeferredQueues;
	if (DeferredDeletionQueue[DeferredDeletionQueueIndex].Queue.Num() > 0)
	{
		if (!DeferredDeletionQueue[DeferredDeletionQueueIndex].Fence.IsFenceComplete())
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManWaitOnRender);
			DeferredDeletionQueue[DeferredDeletionQueueIndex].Fence.Wait();
		}
		DeferredDeletionQueue[DeferredDeletionQueueIndex].Queue.Empty();
	}
}

void FNiagaraWorldManager::Tick(ETickingGroup TickGroup, float DeltaSeconds, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(Niagara);
	LLM_SCOPE(ELLMTag::Niagara);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManTick);
	SCOPE_CYCLE_COUNTER(STAT_NiagaraOverview_GT);

	FNiagaraSharedObject::FlushDeletionList();

	SkeletalMeshGeneratedData.TickGeneratedData(DeltaSeconds);

	// Cache player view locations for all system instances to access
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

	//Tick our collections to push any changes to bound stores.
	for (TPair<UNiagaraParameterCollection*, UNiagaraParameterCollectionInstance*> CollectionInstPair : ParameterCollections)
	{
		check(CollectionInstPair.Value);
		CollectionInstPair.Value->Tick();
	}

	//Now tick all system instances. 
	TArray<UNiagaraSystem*, TInlineAllocator<4>> DeadSystems;
	for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SystemSim : SystemSimulations)
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
		SystemSimulations.Remove(DeadSystem);
	}
}
