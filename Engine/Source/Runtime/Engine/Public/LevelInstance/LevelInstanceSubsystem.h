// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "UObject/GCObject.h"
#include "Folder.h"
#include "LevelInstance/LevelInstanceTypes.h"

#include "LevelInstanceSubsystem.generated.h"

class ALevelInstance;
class ULevelInstanceEditorObject;
class ULevelStreamingLevelInstance;
class ULevelStreamingLevelInstanceEditor;
class UWorldPartitionSubsystem;
class UBlueprint;

/**
 * ULevelInstanceSubsystem
 */
UCLASS()
class ENGINE_API ULevelInstanceSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	ULevelInstanceSubsystem();
	virtual ~ULevelInstanceSubsystem();
		
	//~ Begin USubsystem Interface.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	//~ End USubsystem Interface.
	
	//~ Begin UWorldSubsystem Interface.
	virtual void UpdateStreamingState() override;
	//~ End UWorldSubsystem Interface.

	ALevelInstance* GetLevelInstance(FLevelInstanceID LevelInstanceID) const;
	FLevelInstanceID RegisterLevelInstance(ALevelInstance* LevelInstanceActor);
	void UnregisterLevelInstance(ALevelInstance* LevelInstanceActor);
	void RequestLoadLevelInstance(ALevelInstance* LevelInstanceActor, bool bUpdate);
	void RequestUnloadLevelInstance(ALevelInstance* LevelInstanceActor);
	bool IsLoaded(const ALevelInstance* LevelInstanceActor) const;
	void ForEachLevelInstanceAncestorsAndSelf(AActor* Actor, TFunctionRef<bool(ALevelInstance*)> Operation) const;

#if WITH_EDITOR
	void Tick();
	void OnExitEditorMode();
	void OnTryExitEditorMode();
	bool OnExitEditorModeInternal(bool bForceExit);
	void PackAllLoadedActors();
	bool CanPackAllLoadedActors() const;

	ALevelInstance* GetEditingLevelInstance() const;
	bool CanEditLevelInstance(const ALevelInstance* LevelInstanceActor, FText* OutReason = nullptr) const;
	bool CanCommitLevelInstance(const ALevelInstance* LevelInstanceActor, FText* OutReason = nullptr) const;
	bool CanDiscardLevelInstance(const ALevelInstance* LevelInstanceActor, FText* OutReason = nullptr) const;
	void EditLevelInstance(ALevelInstance* LevelInstanceActor, TWeakObjectPtr<AActor> ContextActorPtr = nullptr);
	bool CommitLevelInstance(ALevelInstance* LevelInstanceActor, bool bDiscardEdits = false, TSet<FName>* DirtyPackages = nullptr);
	bool IsEditingLevelInstanceDirty(const ALevelInstance* LevelInstanceActor) const;
	bool IsEditingLevelInstance(const ALevelInstance* LevelInstanceActor) const { return GetLevelInstanceEdit(LevelInstanceActor) != nullptr; }
	
	bool GetLevelInstanceBounds(const ALevelInstance* LevelInstanceActor, FBox& OutBounds) const;
	static bool GetLevelInstanceBoundsFromPackage(const FTransform& InstanceTransform, FName LevelPackage, FBox& OutBounds);
	
	void ForEachActorInLevelInstance(const ALevelInstance* LevelInstanceActor, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachLevelInstanceAncestorsAndSelf(const AActor* Actor, TFunctionRef<bool(const ALevelInstance*)> Operation) const;
	void ForEachLevelInstanceAncestors(const AActor* Actor, TFunctionRef<bool(const ALevelInstance*)> Operation) const;
	void ForEachLevelInstanceChild(const ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(const ALevelInstance*)> Operation) const;
	void ForEachLevelInstanceChild(ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(ALevelInstance*)> Operation) const;
	bool HasDirtyChildrenLevelInstances(const ALevelInstance* LevelInstanceActor) const;
	
	void SetIsHiddenEdLayer(ALevelInstance* LevelInstanceActor, bool bIsHiddenEdLayer);
	void SetIsTemporarilyHiddenInEditor(ALevelInstance* LevelInstanceActor, bool bIsHidden);

	bool SetCurrent(ALevelInstance* LevelInstanceActor) const;
	bool IsCurrent(const ALevelInstance* LevelInstanceActor) const;
	ALevelInstance* CreateLevelInstanceFrom(const TArray<AActor*>& ActorsToMove, const FNewLevelInstanceParams& CreationParams);
	bool MoveActorsToLevel(const TArray<AActor*>& ActorsToRemove, ULevel* DestinationLevel, TArray<AActor*>* OutActors = nullptr) const;
	bool MoveActorsTo(ALevelInstance* LevelInstanceActor, const TArray<AActor*>& ActorsToMove, TArray<AActor*>* OutActors = nullptr);
	bool BreakLevelInstance(ALevelInstance* LevelInstanceActor, uint32 Levels = 1, TArray<AActor*>* OutMovedActors = nullptr);

	bool CanMoveActorToLevel(const AActor* Actor, FText* OutReason = nullptr) const;
	void OnActorDeleted(AActor* Actor);
	ULevel* GetLevelInstanceLevel(const ALevelInstance* LevelInstanceActor) const;

	bool LevelInstanceHasLevelScriptBlueprint(const ALevelInstance* LevelInstance) const;

	ALevelInstance* GetParentLevelInstance(const AActor* Actor) const;
	void BlockLoadLevelInstance(ALevelInstance* LevelInstanceActor);
	void BlockUnloadLevelInstance(ALevelInstance* LevelInstanceActor);
		
	bool HasChildEdit(const ALevelInstance* LevelInstanceActor) const;
#endif

private:
	void BlockOnLoading();
	void LoadLevelInstance(ALevelInstance* LevelInstanceActor);
	void UnloadLevelInstance(const FLevelInstanceID& LevelInstanceID);
	void ForEachActorInLevel(ULevel* Level, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachLevelInstanceAncestors(AActor* Actor, TFunctionRef<bool(ALevelInstance*)> Operation) const;
	ALevelInstance* GetOwningLevelInstance(const ULevel* Level) const;
	
	void RegisterLoadedLevelStreamingLevelInstance(ULevelStreamingLevelInstance* LevelStreaming);

#if WITH_EDITOR
	void RegisterLoadedLevelStreamingLevelInstanceEditor(ULevelStreamingLevelInstanceEditor* LevelStreaming);

	void OnEditChild(FLevelInstanceID LevelInstanceID);
	void OnCommitChild(FLevelInstanceID LevelInstanceID, bool bChildChanged);

	bool ForEachLevelInstanceChildImpl(const ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(const ALevelInstance*)> Operation) const;
	bool ForEachLevelInstanceChildImpl(ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(ALevelInstance*)> Operation) const;

	void BreakLevelInstance_Impl(ALevelInstance* LevelInstanceActor, uint32 Levels, TArray<AActor*>& OutMovedActors);

	static bool ShouldIgnoreDirtyPackage(UPackage* DirtyPackage, const UWorld* EditingWorld);

	class FLevelInstanceEdit : public FGCObject
	{
	public:
		TObjectPtr<ULevelStreamingLevelInstanceEditor> LevelStreaming;
		TObjectPtr<ULevelInstanceEditorObject> EditorObject;

		FLevelInstanceEdit(ULevelStreamingLevelInstanceEditor* InLevelStreaming, FLevelInstanceID InLevelInstanceID);
		virtual ~FLevelInstanceEdit();

		UWorld* GetEditWorld() const;
		FLevelInstanceID GetLevelInstanceID() const;

		virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
		virtual FString GetReferencerName() const override;

		void GetPackagesToSave(TArray<UPackage*>& OutPackagesToSave) const;
		bool CanDiscard(FText* OutReason = nullptr) const;
		bool HasCommittedChanges() const;
		void MarkCommittedChanges();
	};

	void ResetEdit(TUniquePtr<FLevelInstanceEdit>& InLevelInstanceEdit);
	bool EditLevelInstanceInternal(ALevelInstance* LevelInstanceActor, TWeakObjectPtr<AActor> ContextActorPtr, bool bRecursive);
	bool CommitLevelInstanceInternal(TUniquePtr<FLevelInstanceEdit>& InLevelInstanceEdit, bool bDiscardEdits = false, bool bDiscardOnFailure = false, TSet<FName>* DirtyPackages = nullptr);
	
	const FLevelInstanceEdit* GetLevelInstanceEdit(const ALevelInstance* LevelInstanceActor) const;
	bool IsLevelInstanceEditDirty(const FLevelInstanceEdit* LevelInstanceEdit) const;
	
	struct FLevelsToRemoveScope
	{
		FLevelsToRemoveScope(ULevelInstanceSubsystem* InOwner);
		~FLevelsToRemoveScope();
		bool IsValid() const { return !bIsBeingDestroyed; }

		TArray<ULevel*> Levels;
		TWeakObjectPtr<ULevelInstanceSubsystem> Owner;
		bool bResetTrans = false;
		bool bIsBeingDestroyed = false;
	};
	
	void RemoveLevelsFromWorld(const TArray<ULevel*>& Levels, bool bResetTrans = true);
#endif
	friend ULevelStreamingLevelInstance;
	friend ULevelStreamingLevelInstanceEditor;

#if WITH_EDITOR
	bool bIsCreatingLevelInstance;
	bool bIsCommittingLevelInstance;
#endif

	struct FLevelInstance
	{
		ULevelStreamingLevelInstance* LevelStreaming = nullptr;
	};

	TMap<ALevelInstance*, bool> LevelInstancesToLoadOrUpdate;
	TSet<FLevelInstanceID> LevelInstancesToUnload;
	TMap<FLevelInstanceID, FLevelInstance> LevelInstances;
	TMap<FLevelInstanceID, ALevelInstance*> RegisteredLevelInstances;

#if WITH_EDITORONLY_DATA
	// Optional scope to accelerate level unload by batching them
	TUniquePtr<FLevelsToRemoveScope> LevelsToRemoveScope;

	TUniquePtr<FLevelInstanceEdit> LevelInstanceEdit;

	TMap<FLevelInstanceID, int32> ChildEdits;
#endif
};