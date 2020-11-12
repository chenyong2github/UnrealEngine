// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Containers/Set.h"
#include "Tickable.h"
#include "Stats/Stats.h"

#if WITH_EDITOR

class UStaticMesh;
class UPrimitiveComponent;
class UStaticMeshComponent;
class FQueuedThreadPool;
struct FAssetCompileContext;
enum class EQueuedWorkPriority : uint8;

class FStaticMeshCompilingManager
{
public:
	ENGINE_API static FStaticMeshCompilingManager& Get();

	/**
	 * Returns true if the feature is currently activated.
	 */
	ENGINE_API bool IsAsyncStaticMeshCompilationEnabled() const;

	/** 
	 * Returns the number of outstanding texture compilations.
	 */
	ENGINE_API int32 GetNumRemainingMeshes() const;

	/** 
	 * Adds static meshes compiled asynchronously so they are monitored. 
	 */
	ENGINE_API void AddStaticMeshes(const TArray<UStaticMesh*>& InStaticMeshes);

	/** 
	 * Blocks until completion of the requested static meshes.
	 */
	ENGINE_API void FinishCompilation(const TArray<UStaticMesh*>& InStaticMeshes);

	/** 
	 * Blocks until completion of all async static mesh compilation.
	 */
	ENGINE_API void FinishAllCompilation();

	/**
	 * Returns if asynchronous compilation is allowed for this static mesh.
	 */
	ENGINE_API bool IsAsyncCompilationAllowed(UStaticMesh* InStaticMesh) const;

	/**
	 * Returns the priority at which the given static mesh should be scheduled.
	 */
	ENGINE_API EQueuedWorkPriority GetBasePriority(UStaticMesh* InStaticMesh) const;

	/**
	 * Returns the threadpool where static mesh compilation should be scheduled.
	 */
	ENGINE_API FQueuedThreadPool* GetThreadPool() const;

	/**
	 * Cancel any pending work and blocks until it is safe to shut down.
	 */
	ENGINE_API void Shutdown();

	/** Called once per frame, fetches completed tasks and applies them to the scene. */
	ENGINE_API void ProcessAsyncTasks(bool bLimitExecutionTime = false);

private:
	friend class FAssetCompilingManager;
	
	bool bHasShutdown = false;
	TSet<TWeakObjectPtr<UStaticMesh>> RegisteredStaticMesh;
	void FinishCompilationsForGame();
	void Reschedule();
	void ProcessStaticMeshes(bool bLimitExecutionTime, int32 MinBatchSize = 1);
	void UpdateCompilationNotification();
	void PostStaticMeshesCompilation(const TSet<UStaticMesh*>& InStaticMeshes);
	void FinishStaticMeshCompilation(UStaticMesh* StaticMesh);
};

#endif // #if WITH_EDITOR