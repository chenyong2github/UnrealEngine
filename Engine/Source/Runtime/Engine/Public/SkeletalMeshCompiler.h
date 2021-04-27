// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Containers/Set.h"
#include "Tickable.h"
#include "Stats/Stats.h"
#include "AssetCompilingManager.h"
#include "AsyncCompilationHelpers.h"

#if WITH_EDITOR

class USkeletalMesh;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class FQueuedThreadPool;
struct FAssetCompileContext;
enum class EQueuedWorkPriority : uint8;

class FSkeletalMeshCompilingManager : IAssetCompilingManager
{
public:
	ENGINE_API static FSkeletalMeshCompilingManager& Get();

	/**
	 * Returns true if the feature is currently activated.
	 */
	ENGINE_API bool IsAsyncCompilationEnabled() const;

	/** 
	 * Returns the number of outstanding compilations.
	 */
	ENGINE_API int32 GetNumRemainingJobs() const;

	/** 
	 * Adds skeletal meshes compiled asynchronously so they are monitored. 
	 */
	ENGINE_API void AddSkeletalMeshes(TArrayView<USkeletalMesh* const> InSkeletalMeshes);

	/** 
	 * Blocks until completion of the requested skeletal meshes.
	 */
	ENGINE_API void FinishCompilation(TArrayView<USkeletalMesh* const> InSkeletalMeshes);

	/** 
	 * Blocks until completion of all async skeletal mesh compilation.
	 */
	ENGINE_API void FinishAllCompilation() override;

	/**
	 * Returns if asynchronous compilation is allowed for this skeletal mesh.
	 */
	ENGINE_API bool IsAsyncCompilationAllowed(USkeletalMesh* InSkeletalMesh) const;

	/**
	 * Returns the priority at which the given skeletal mesh should be scheduled.
	 */
	ENGINE_API EQueuedWorkPriority GetBasePriority(USkeletalMesh* InSkeletalMesh) const;

	/**
	 * Returns the threadpool where skeletal mesh compilation should be scheduled.
	 */
	ENGINE_API FQueuedThreadPool* GetThreadPool() const;

	/**
	 * Cancel any pending work and blocks until it is safe to shut down.
	 */
	ENGINE_API void Shutdown() override;

private:
	FSkeletalMeshCompilingManager();

	FName GetAssetTypeName() const override;
	FTextFormat GetAssetNameFormat() const override;
	TArrayView<FName> GetDependentTypeNames() const override;
	int32 GetNumRemainingAssets() const override;
	void ProcessAsyncTasks(bool bLimitExecutionTime = false) override;

	friend class FAssetCompilingManager;
	
	bool bHasShutdown = false;
	TSet<TWeakObjectPtr<USkeletalMesh>> RegisteredSkeletalMesh;
	FAsyncCompilationNotification Notification;
	void FinishCompilationsForGame();
	void Reschedule();
	void ProcessSkeletalMeshes(bool bLimitExecutionTime, int32 MinBatchSize = 1);
	void UpdateCompilationNotification();

	void PostCompilation(USkeletalMesh* SkeletalMesh);
	void PostCompilation(TArrayView<USkeletalMesh* const> InSkeletalMeshes);
};

#endif // #if WITH_EDITOR