// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "Foundation/FoundationTypes.h"
#include "Tickable.h"

#if WITH_EDITOR
#include "WorldPartition/Foundation/FoundationActorDescFactory.h"
#endif

#include "FoundationSubsystem.generated.h"

class AFoundationActor;
class ULevelStreamingFoundationInstance;
class ULevelStreamingFoundationEditor;
class UWorldPartitionSubsystem;

/**
 * UFoundationSubsystem
 */
UCLASS()
class ENGINE_API UFoundationSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UFoundationSubsystem();
	virtual ~UFoundationSubsystem();
		
	//~ Begin USubsystem Interface.
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem Interface.
	
	//~ Begin UWorldSubsystem Interface.
	virtual void UpdateStreamingState() override;
	//~ End UWorldSubsystem Interface.

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaSeconds) override;
	virtual bool IsTickableInEditor() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;
	virtual ETickableTickType GetTickableTickType() const override;
	virtual TStatId GetStatId() const override;
	//~End FTickableGameObject

	AFoundationActor* GetFoundation(FFoundationID FoundationID) const;
	FFoundationID RegisterFoundation(AFoundationActor* FoundationActor);
	void UnregisterFoundation(AFoundationActor* FoundationActor);
	void RequestLoadFoundation(AFoundationActor* FoundationActor, bool bUpdate);
	void RequestUnloadFoundation(AFoundationActor* FoundationActor);
	bool IsLoaded(const AFoundationActor* FoundationActor) const;
	void ForEachFoundationAncestorsAndSelf(AActor* Actor, TFunctionRef<bool(AFoundationActor*)> Operation) const;

#if WITH_EDITOR
	bool CanEditFoundation(const AFoundationActor* FoundationActor, FText* OutReason = nullptr) const;
	bool CanCommitFoundation(const AFoundationActor* FoundationActor, FText* OutReason = nullptr) const;
	void EditFoundation(AFoundationActor* FoundationActor, TWeakObjectPtr<AActor> ContextActorPtr = nullptr);
	void CommitFoundation(AFoundationActor* FoundationActor, bool bDiscardEdits = false);
	void SaveFoundationAs(AFoundationActor* FoundationActor);
	bool IsEditingFoundationDirty(const AFoundationActor* FoundationActor) const;
	bool IsEditingFoundation(const AFoundationActor* FoundationActor) const { return GetFoundationEdit(FoundationActor) != nullptr; }
	
	FFoundationActorDescFactory* GetActorDescFactory() const { return FoundationActorDescFactory.Get(); }
	bool GetFoundationBounds(const AFoundationActor* FoundationActor, FBox& OutBounds) const;
	
	void ForEachActorInFoundation(const AFoundationActor* FoundationActor, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachFoundationAncestorsAndSelf(const AActor* Actor, TFunctionRef<bool(const AFoundationActor*)> Operation) const;
	void ForEachFoundationAncestors(const AActor* Actor, TFunctionRef<bool(const AFoundationActor*)> Operation) const;
	void ForEachFoundationChildren(const AFoundationActor* FoundationActor, bool bRecursive, TFunctionRef<bool(const AFoundationActor*)> Operation) const;
	void ForEachFoundationChildren(AFoundationActor* FoundationActor, bool bRecursive, TFunctionRef<bool(AFoundationActor*)> Operation) const;
	void ForEachFoundationEdit(TFunctionRef<bool(AFoundationActor*)> Operation) const;
	bool HasDirtyChildrenFoundations(const AFoundationActor* FoundationActor) const;

	void SetIsTemporarilyHiddenInEditor(AFoundationActor* FoundationActor, bool bIsHidden);

	bool SetCurrent(AFoundationActor* FoundationActor) const;
	bool IsCurrent(const AFoundationActor* FoundationActor) const;
	AFoundationActor* CreateFoundationFrom(const TArray<AActor*>& ActorsToMove, UWorld* TemplateWorld = nullptr);
	bool MoveActorsToLevel(const TArray<AActor*>& ActorsToRemove, ULevel* DestinationLevel) const;
	bool MoveActorsTo(AFoundationActor* FoundationActor, const TArray<AActor*>& ActorsToMove);
	bool BreakFoundation(AFoundationActor* FoundationActor, uint32 Levels = 1);

	bool CanMoveActorToLevel(const AActor* Actor) const;
	void DiscardEdits();
	void OnActorDeleted(AActor* Actor);
	ULevel* GetFoundationLevel(const AFoundationActor* FoundationActor) const;

	AFoundationActor* GetParentFoundation(const AActor* Actor) const;
#endif

private:

	void LoadFoundation(AFoundationActor* FoundationActor);
	void UnloadFoundation(const FFoundationID& FoundationID);
	void ForEachActorInLevel(ULevel* Level, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachFoundationAncestors(AActor* Actor, TFunctionRef<bool(AFoundationActor*)> Operation) const;
	AFoundationActor* GetOwningFoundation(const ULevel* Level) const;
	FFoundationID ComputeFoundationID(AFoundationActor* FoundationActor) const;
#if WITH_EDITOR
	void CommitChildrenFoundations(AFoundationActor* FoundationActor);

	void RegisterActorDescFactories(UWorldPartitionSubsystem* WorldPartitionSubsystem);
	static bool ShouldIgnoreDirtyPackage(UPackage* DirtyPackage, const UWorld* EditingWorld);

	struct FFoundationEdit
	{
		ULevelStreamingFoundationEditor* LevelStreaming = nullptr;

		UWorld* GetEditWorld() const;
	};

	const FFoundationEdit* GetFoundationEdit(const AFoundationActor* FoundationActor) const;
	bool IsFoundationEditDirty(const FFoundationEdit* FoundationEdit) const;
	
	struct FLevelsToRemoveScope
	{
		~FLevelsToRemoveScope();

		TArray<ULevel*> Levels;
		bool bResetTrans = false;
	};
	
	friend ULevelStreamingFoundationInstance;
	void RemoveLevelFromWorld(ULevel* Level, bool bResetTrans);
#endif


#if WITH_EDITORONLY_DATA
	// Optional scope to accelerate level unload by batching them
	TUniquePtr<FLevelsToRemoveScope> LevelsToRemoveScope;
	
	TMap<FName, FFoundationEdit> FoundationEdits;

	TUniquePtr<FFoundationActorDescFactory> FoundationActorDescFactory;
#endif

	struct FFoundationInstance
	{
		ULevelStreamingFoundationInstance* LevelStreaming = nullptr;
	};

	TMap<AFoundationActor*, bool> FoundationsToLoadOrUpdate;
	TSet<FFoundationID> FoundationsToUnload;
	TMap<FFoundationID, FFoundationInstance> FoundationInstances;
	TMap<FFoundationID, AFoundationActor*> RegisteredFoundations;
	FFoundationID PendingFoundationToEdit = InvalidFoundationID;
};