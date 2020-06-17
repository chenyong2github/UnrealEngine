// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Containers/Set.h"
#include "Tickable.h"
#include "Stats/Stats.h"

#if WITH_EDITOR

class UTexture;
class FQueuedThreadPool;
enum class EQueuedWorkPriority : uint8;

class FTextureCompilingManager : public FTickableGameObject
{
public:
	ENGINE_API static FTextureCompilingManager& Get();

	/** 
	 * Returns the number of outstanding texture compilations.
	 */
	ENGINE_API int32 GetNumRemainingTextures() const;

	/** 
	 * Adds textures compiled asynchronously so they are monitored. 
	 */
	ENGINE_API void AddTextures(const TArray<UTexture*>& InTextures);

	/** 
	 * Blocks until completion of the requested textures.
	 */
	ENGINE_API void FinishCompilation(const TArray<UTexture*>& InTextures);

	/** 
	 * Blocks until completion of all async texture compilation.
	 */
	ENGINE_API void FinishAllCompilation();

	/**
	 * Returns if asynchronous compilation is allowed for this texture.
	 */
	ENGINE_API bool IsAsyncCompilationAllowed(UTexture* InTexture) const;

	/**
	 * Returns the priority at which the given texture should be scheduled.
	 */
	ENGINE_API EQueuedWorkPriority GetBasePriority(UTexture* InTexture) const;

	/**
	 * Returns the threadpool where texture compilation should be scheduled.
	 */
	ENGINE_API FQueuedThreadPool* GetThreadPool() const;

protected:
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual bool IsTickableInEditor() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(FTextureCompilingManager, STATGROUP_Tickables); }

private:
	TArray<TSet<TWeakObjectPtr<UTexture>>> RegisteredTextureBuckets;

	void ProcessTextures(int32 MaximumPriority = -1);
	bool IsAsyncTextureCompilationEnabled() const;
	void UpdateCompilationNotification();
	void FinishTextureCompilation(UTexture* Texture);
};

#endif