// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Containers/Set.h"
#include "AsyncCompilationHelpers.h"

#if WITH_EDITOR

class UTexture;
class FQueuedThreadPool;
enum class EQueuedWorkPriority : uint8;

class FTextureCompilingManager
{
public:
	ENGINE_API static FTextureCompilingManager& Get();

	/**
	 * Returns true if the feature is currently activated.
	 */
	ENGINE_API bool IsAsyncTextureCompilationEnabled() const;

	/** 
	 * Returns the number of outstanding texture compilations.
	 */
	ENGINE_API int32 GetNumRemainingTextures() const;

	/** 
	 * Adds textures compiled asynchronously so they are monitored. 
	 */
	ENGINE_API void AddTextures(TArrayView<UTexture* const> InTextures);

	/** 
	 * Blocks until completion of the requested textures.
	 */
	ENGINE_API void FinishCompilation(TArrayView<UTexture* const> InTextures);

	/** 
	 * Blocks until completion of all async texture compilation.
	 */
	ENGINE_API void FinishAllCompilation();

	/**
	 * Returns if asynchronous compilation is allowed for this texture.
	 */
	ENGINE_API bool IsAsyncCompilationAllowed(UTexture* InTexture) const;

	/**
	 * Request that the texture be processed at the specified priority.
	 */
	ENGINE_API bool RequestPriorityChange(UTexture* InTexture, EQueuedWorkPriority Priority);

	/**
	 * Returns the priority at which the given texture should be scheduled.
	 */
	ENGINE_API EQueuedWorkPriority GetBasePriority(UTexture* InTexture) const;

	/**
	 * Returns the threadpool where texture compilation should be scheduled.
	 */
	ENGINE_API FQueuedThreadPool* GetThreadPool() const;

	/**
	 * Cancel any pending work and blocks until it is safe to shut down.
	 */
	ENGINE_API void Shutdown();


private:
	friend class FAssetCompilingManager;

	FTextureCompilingManager();
	
	void ProcessAsyncTasks(bool bLimitExecutionTime = false);
	
	void FinishCompilationsForGame();
	void ProcessTextures(bool bLimitExecutionTime, int32 MaximumPriority = -1);
	void UpdateCompilationNotification();
	
	void PostCompilation(UTexture* Texture);
	void PostCompilation(TArrayView<UTexture* const> InCompiledTextures);

	bool bHasShutdown = false;
	TArray<TSet<TWeakObjectPtr<UTexture>>> RegisteredTextureBuckets;
	FAsyncCompilationNotification Notification;
};

#endif