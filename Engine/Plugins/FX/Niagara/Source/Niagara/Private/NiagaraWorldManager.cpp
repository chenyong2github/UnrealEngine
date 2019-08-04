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

DECLARE_CYCLE_STAT(TEXT("Niagara Manager Tick [GT]"), STAT_NiagaraWorldManTick, STATGROUP_Niagara);
DECLARE_CYCLE_STAT(TEXT("Niagara Manager Wait On Render [GT]"), STAT_NiagaraWorldManWaitOnRender, STATGROUP_Niagara);

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

FNiagaraWorldManager::FNiagaraWorldManager(UWorld* InWorld)
	: World(InWorld)
	, CachedEffectsQuality(INDEX_NONE)
{
}

FNiagaraWorldManager* FNiagaraWorldManager::Get(UWorld* World)
{
	//INiagaraModule& NiagaraModule = FModuleManager::LoadModuleChecked<INiagaraModule>("Niagara");
	return INiagaraModule::GetWorldManager(World);
}

void FNiagaraWorldManager::AddReferencedObjects(FReferenceCollector& Collector)
{
	// World doesn't need to be added to the reference list. It will be handle via OnWorldInit & OnWorldCleanup & OnPreWorldFinishDestroy in INiagaraModule

	Collector.AddReferencedObjects(ParameterCollections);
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

void FNiagaraWorldManager::OnBatcherDestroyed(NiagaraEmitterInstanceBatcher* InBatcher)
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

void FNiagaraWorldManager::Tick(float DeltaSeconds)
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
		for ( FConstPlayerControllerIterator Iterator=World->GetPlayerControllerIterator(); Iterator; ++Iterator)
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
	TArray<UNiagaraSystem*> DeadSystems;
	for (TPair<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>>& SystemSim : SystemSimulations)
	{
		if (SystemSim.Value->Tick(DeltaSeconds) == false)
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
	if ( DeferredDeletionQueue[DeferredDeletionQueueIndex].Queue.Num() > 0 )
	{
		DeferredDeletionQueue[DeferredDeletionQueueIndex].Fence.BeginFence();
	}

	// Remove instances from oldest frame making sure they aren't in use on the RT
	DeferredDeletionQueueIndex = (DeferredDeletionQueueIndex + 1) % NumDeferredQueues;
	if ( DeferredDeletionQueue[DeferredDeletionQueueIndex].Queue.Num() > 0 )
	{
		if ( !DeferredDeletionQueue[DeferredDeletionQueueIndex].Fence.IsFenceComplete() )
		{
			SCOPE_CYCLE_COUNTER(STAT_NiagaraWorldManWaitOnRender);
			DeferredDeletionQueue[DeferredDeletionQueueIndex].Fence.Wait();
		}
		DeferredDeletionQueue[DeferredDeletionQueueIndex].Queue.Empty();
	}
}
