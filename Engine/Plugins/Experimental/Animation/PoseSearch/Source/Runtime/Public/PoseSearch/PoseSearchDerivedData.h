// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PoseSearch/PoseSearch.h"

#if WITH_EDITOR
#include "TickableEditorObject.h"
#include "UObject/ObjectSaveContext.h"

namespace UE::PoseSearch
{
	struct FPoseSearchDatabaseAsyncCacheTask;
	class FPoseSearchDatabaseAsyncCacheTasks;

	class POSESEARCH_API FAsyncPoseSearchDatabasesManagement : public FTickableEditorObject, public FTickableCookObject, public FGCObject
	{
	public:
		~FAsyncPoseSearchDatabasesManagement();
		static FAsyncPoseSearchDatabasesManagement& Get();
		void RequestAsyncBuildIndex(UPoseSearchDatabase& Database, bool bWaitForCompletion = false, bool bCancelPreviousTask = false);
		void WaitOnExistingBuildIndex(const UPoseSearchDatabase& Database, bool bWantResults);
		bool IsBuildingIndex(const UPoseSearchDatabase& Database) const;

	private:
		FAsyncPoseSearchDatabasesManagement();

		void ForEachPoseSearchDatabase(bool bUseTasksDatabases, TFunctionRef<void(UPoseSearchDatabase&)> InFunction);
		void ExecuteIfObjectIsReferencedByDatabase(UObject* Object, bool bUseTasksDatabases, TFunctionRef<void(UPoseSearchDatabase&)> InFunction);

		void OnObjectPreSave(UObject* SavedObject, FObjectPreSaveContext SaveContext);
		void OnPreObjectPropertyChanged(UObject* Object, const class FEditPropertyChain& PropChain);
		void OnObjectPropertyChanged(UObject* Object, struct FPropertyChangedEvent& Event);

		FPoseSearchDatabaseAsyncCacheTask& GetTask(int32 TaskIndex);
		const FPoseSearchDatabaseAsyncCacheTask& GetTask(int32 TaskIndex) const;
		void RemoveTask(int32 TaskIndex);

		void Shutdown();
		void StartQueuedTasks(int32 MaxActiveTasks);

		// Begin FTickableEditorObject
		virtual void Tick(float DeltaTime) override;
		virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Always; }
		virtual TStatId GetStatId() const override;
		// End FTickableEditorObject

		// Begin FTickableCookObject
		virtual void TickCook(float DeltaTime, bool bCookCompete) override;
		// End FTickableCookObject

		// Begin FGCObject
		void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override { return TEXT("FAsyncPoseSearchDatabaseManagement"); }
		// End FGCObject

		FPoseSearchDatabaseAsyncCacheTasks& Tasks;

		FDelegateHandle OnObjectPreSaveHandle;
		FDelegateHandle OnObjectPropertyChangedHandle;
		FDelegateHandle OnPreObjectPropertyChangedHandle;
	};
} // namespace UE::PoseSearch

#endif // WITH_EDITOR