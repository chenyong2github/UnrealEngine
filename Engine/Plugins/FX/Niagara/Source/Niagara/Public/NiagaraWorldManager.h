// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "NiagaraParameterCollection.h"
#include "UObject/GCObject.h"
#include "NiagaraDataSet.h"
#include "NiagaraScriptExecutionContext.h"
#include "NiagaraSystemSimulation.h"
#include "NiagaraSystemInstance.h"
#include "GlobalDistanceFieldParameters.h"
#include "NiagaraDataInterfaceSkeletalMesh.h"

class UWorld;
class UNiagaraParameterCollection;
class UNiagaraParameterCollectionInstance;



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


/**
* Manager class for any data relating to a particular world.
*/
class FNiagaraWorldManager : public FGCObject
{
public:
	
	FNiagaraWorldManager(UWorld* InWorld);
	static FNiagaraWorldManager* Get(UWorld* World);

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

	// Gamethread callback to cleanup references to the given batcher before it gets deleted on the renderthread.
	void OnBatcherDestroyed(NiagaraEmitterInstanceBatcher* InBatcher);

	void Tick(float DeltaSeconds);

	void OnWorldCleanup(bool bSessionEnded, bool bCleanupResources);
	
	FORCEINLINE FNDI_SkeletalMesh_GeneratedData& GetSkeletalMeshGeneratedData() { return SkeletalMeshGeneratedData; }

	bool CachedPlayerViewLocationsValid() const { return bCachedPlayerViewLocationsValid; }
	TArrayView<const FVector> GetCachedPlayerViewLocations() const { check(bCachedPlayerViewLocationsValid); return MakeArrayView(CachedPlayerViewLocations); }

private:
	UWorld* World;

	TMap<UNiagaraParameterCollection*, UNiagaraParameterCollectionInstance*> ParameterCollections;

	TMap<UNiagaraSystem*, TSharedRef<FNiagaraSystemSimulation, ESPMode::ThreadSafe>> SystemSimulations;

	int32 CachedEffectsQuality;

	bool bCachedPlayerViewLocationsValid = false;
	TArray<FVector, TInlineAllocator<8> > CachedPlayerViewLocations;

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

