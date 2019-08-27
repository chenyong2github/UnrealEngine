// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/World.h"
#include "NiagaraParameterCollection.h"
#include "UObject/GCObject.h"
#include "NiagaraDataSet.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraSystemSimulation.h"
#include "NiagaraSystemInstance.h"
#include "GlobalDistanceFieldParameters.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"
#include "NiagaraComponentPool.h"

#include "NiagaraWorldManager.generated.h"

class UWorld;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;
class UNiagaraComponentPool;


class FNiagaraViewDataMgr : public FRenderResource
{
public:
	FNiagaraViewDataMgr();

	static void Init();
	static void Shutdown();

	void PostOpaqueRender(FPostOpaqueRenderParameters& Params)
	{
		SceneDepthTexture = Params.DepthTexture;
		ViewUniformBuffer = Params.ViewUniformBuffer;
		SceneNormalTexture = Params.NormalTexture;
		SceneTexturesUniformParams = Params.SceneTexturesUniformParams;
	}

	FRHITexture2D* GetSceneDepthTexture() { return SceneDepthTexture; }
	FRHITexture2D* GetSceneNormalTexture() { return SceneNormalTexture; }
	FRHIUniformBuffer* GetViewUniformBuffer() { return ViewUniformBuffer; }
	TUniformBufferRef<FSceneTexturesUniformParameters> GetSceneTextureUniformParameters() { return SceneTexturesUniformParams; }

	virtual void InitDynamicRHI() override;

	virtual void ReleaseDynamicRHI() override;

private:
	FRHITexture2D* SceneDepthTexture;
	FRHITexture2D* SceneNormalTexture;
	FRHIUniformBuffer* ViewUniformBuffer;

	TUniformBufferRef<FSceneTexturesUniformParameters> SceneTexturesUniformParams;
	FPostOpaqueRenderDelegate PostOpaqueDelegate;
};

extern TGlobalResource<FNiagaraViewDataMgr> GNiagaraViewDataManager;

USTRUCT()
struct FNiagaraWorldManagerTickFunction : public FTickFunction
{
	GENERATED_USTRUCT_BODY()

	//~ FTickFunction Interface
	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed)  override;
	//~ FTickFunction Interface

	FNiagaraWorldManager* Owner;
};

template<>
struct TStructOpsTypeTraits<FNiagaraWorldManagerTickFunction> : public TStructOpsTypeTraitsBase2<FNiagaraWorldManagerTickFunction>
{
	enum
	{
		WithCopy = false
	};
};

/**
* Manager class for any data relating to a particular world.
*/
class FNiagaraWorldManager : public FGCObject
{
public:
	
	FNiagaraWorldManager(UWorld* InWorld);
	~FNiagaraWorldManager();

	static FNiagaraWorldManager* Get(UWorld* World);
	static void OnStartup();
	static void OnShutdown();

	// Gamethread callback to cleanup references to the given batcher before it gets deleted on the renderthread.
	static void OnBatcherDestroyed(class NiagaraEmitterInstanceBatcher* InBatcher);

	static void DestroyAllSystemSimulations(class UNiagaraSystem* System);

	//~ GCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	//~ GCObject Interface
	
	UNiagaraParameterCollectionInstance* GetParameterCollection(UNiagaraParameterCollection* Collection);
	void SetParameterCollection(UNiagaraParameterCollectionInstance* NewInstance);
	void CleanupParameterCollections();
	TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe> GetSystemSimulation(UNiagaraSystem* System);
	void DestroySystemSimulation(UNiagaraSystem* System);
	void DestroySystemInstance(TUniquePtr<FNiagaraSystemInstance>& InPtr);	

	void Tick(ETickingGroup TickGroup, float DeltaSeconds, ELevelTick TickType, ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

	/** Called after all actor tick groups are complete. */
	void PostActorTick(float DeltaSeconds);

	void OnWorldCleanup(bool bSessionEnded, bool bCleanupResources);
	
	FORCEINLINE FNDI_SkeletalMesh_GeneratedData& GetSkeletalMeshGeneratedData() { return SkeletalMeshGeneratedData; }

	bool CachedPlayerViewLocationsValid() const { return bCachedPlayerViewLocationsValid; }
	TArrayView<const FVector> GetCachedPlayerViewLocations() const { check(bCachedPlayerViewLocationsValid); return MakeArrayView(CachedPlayerViewLocations); }

	UNiagaraComponentPool* GetComponentPool() { return ComponentPool; }

private:

	// Callback function registered with global world delegates to instantiate world manager when a game world is created
	static void OnWorldInit(UWorld* World, const UWorld::InitializationValues IVS);

	// Callback function registered with global world delegates to cleanup world manager contents
	static void OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	// Callback function registered with global world delegates to cleanup world manager when a game world is destroyed
	static void OnPreWorldFinishDestroy(UWorld* World);

	// Called when the world begins to be torn down for example by level streaming.
	static void OnWorldBeginTearDown(UWorld* World);

	// Callback for when a world is ticked.
	static void TickWorld(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	
	// Gamethread callback to cleanup references to the given batcher before it gets deleted on the renderthread.
	void OnBatcherDestroyed_Internal(NiagaraEmitterInstanceBatcher* InBatcher);

	static FDelegateHandle OnWorldInitHandle;
	static FDelegateHandle OnWorldCleanupHandle;
	static FDelegateHandle OnPreWorldFinishDestroyHandle;
	static FDelegateHandle OnWorldBeginTearDownHandle;
	static FDelegateHandle TickWorldHandle;

	static TMap<class UWorld*, class FNiagaraWorldManager*> WorldManagers;

	UWorld* World;

	FNiagaraWorldManagerTickFunction TickFunction;//TODO: Add more tick functions to kick some work earlier in frame.

	TMap<UNiagaraParameterCollection*, UNiagaraParameterCollectionInstance*> ParameterCollections;

	TMap<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>> SystemSimulations;

	int32 CachedEffectsQuality;

	bool bCachedPlayerViewLocationsValid = false;
	TArray<FVector, TInlineAllocator<8> > CachedPlayerViewLocations;

	UNiagaraComponentPool* ComponentPool;

	/** Generated data used by data interfaces*/
	FNDI_SkeletalMesh_GeneratedData SkeletalMeshGeneratedData;

	// Deferred deletion queue for system instances
	// We need to make sure that any enqueued GPU ticks have been processed before we remove the system instances
	struct FDeferredDeletionQueue
	{
		FRenderCommandFence							Fence;
		TArray<TUniquePtr<FNiagaraSystemInstance>>	Queue;
	};

	static constexpr int NumDeferredQueues = 3;
	int DeferredDeletionQueueIndex = 0;
	FDeferredDeletionQueue DeferredDeletionQueue[NumDeferredQueues];
};

