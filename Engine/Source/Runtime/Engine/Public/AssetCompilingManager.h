// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/Class.h"
#include "UObject/ObjectMacros.h"
#include "Interfaces/Interface_AsyncCompilation.h"
#include "AssetCompilingManager.generated.h"

USTRUCT(BlueprintType)
struct FAssetCompileData
{
	GENERATED_BODY();

	/** Object being compiled */
	UPROPERTY(BlueprintReadWrite, Category = AssetCompileData)
	TWeakObjectPtr<UObject> Asset;

	FAssetCompileData()
	{}

	FAssetCompileData(const TWeakObjectPtr<UObject>& InAsset)
		: Asset(InAsset)
	{
	}
};

#if WITH_EDITOR

DECLARE_MULTICAST_DELEGATE_OneParam(FAssetPostCompileEvent, const TArray<FAssetCompileData>&);

class FAssetCompilingManager
{
public:
	ENGINE_API static FAssetCompilingManager& Get();

	/** 
	 * Returns the number of outstanding texture compilations.
	 */
	ENGINE_API int32 GetNumRemainingAssets() const;

	/** 
	 * Blocks until completion of all assets.
	 */
	ENGINE_API void FinishAllCompilation();

	/**
	 * Event called after an asset finishes compilation.
	 */
	FAssetPostCompileEvent& OnAssetPostCompileEvent() { return AssetPostCompileEvent; }

	/**
	 * Cancel any pending work and blocks until it is safe to shut down.
	 */
	ENGINE_API void Shutdown();

	/**
	 * Returns the thread-pool where asset compilation should be scheduled.
	 */
	ENGINE_API FQueuedThreadPool* GetThreadPool() const;

	/** Called once per frame, fetches completed tasks and applies them to the scene. */
	ENGINE_API void ProcessAsyncTasks(bool bLimitExecutionTime = false);

private:
	friend class FStaticMeshCompilingManager;
	friend class FTextureCompilingManager;

	bool bHasShutdown = false;
	
	/** Event issued at the end of the compile process */
	FAssetPostCompileEvent AssetPostCompileEvent;
};

#endif //#if WITH_EDITOR