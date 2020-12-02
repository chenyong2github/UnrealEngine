// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "LevelInstance/LevelInstanceTypes.h"

#include "LevelInstanceSubsystem.generated.h"

class ALevelInstance;
class ULevelStreamingLevelInstance;
class ULevelStreamingLevelInstanceEditor;
class UWorldPartitionSubsystem;
class UBlueprint;

UENUM()
enum class ELevelInstanceCreationType : uint8
{
	LevelInstance,
	PackedLevelInstance,
	PackedLevelInstanceBlueprint
};

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
	virtual void Deinitialize() override;
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
	void PackLevelInstances();
	bool CanPackLevelInstances() const;

	bool CanEditLevelInstance(const ALevelInstance* LevelInstanceActor, FText* OutReason = nullptr) const;
	bool CanCommitLevelInstance(const ALevelInstance* LevelInstanceActor, FText* OutReason = nullptr) const;
	void EditLevelInstance(ALevelInstance* LevelInstanceActor, TWeakObjectPtr<AActor> ContextActorPtr = nullptr);
	ALevelInstance* CommitLevelInstance(ALevelInstance* LevelInstanceActor, bool bDiscardEdits = false);
	void SaveLevelInstanceAs(ALevelInstance* LevelInstanceActor);
	bool IsEditingLevelInstanceDirty(const ALevelInstance* LevelInstanceActor) const;
	bool IsEditingLevelInstance(const ALevelInstance* LevelInstanceActor) const { return GetLevelInstanceEdit(LevelInstanceActor) != nullptr; }
	
	bool GetLevelInstanceBounds(const ALevelInstance* LevelInstanceActor, FBox& OutBounds) const;
	
	void ForEachActorInLevelInstance(const ALevelInstance* LevelInstanceActor, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachLevelInstanceAncestorsAndSelf(const AActor* Actor, TFunctionRef<bool(const ALevelInstance*)> Operation) const;
	void ForEachLevelInstanceAncestors(const AActor* Actor, TFunctionRef<bool(const ALevelInstance*)> Operation) const;
	void ForEachLevelInstanceChildren(const ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(const ALevelInstance*)> Operation) const;
	void ForEachLevelInstanceChildren(ALevelInstance* LevelInstanceActor, bool bRecursive, TFunctionRef<bool(ALevelInstance*)> Operation) const;
	void ForEachLevelInstanceEdit(TFunctionRef<bool(ALevelInstance*)> Operation) const;
	bool HasDirtyChildrenLevelInstances(const ALevelInstance* LevelInstanceActor) const;
	bool HasEditingChildrenLevelInstances(const ALevelInstance* LevelInstanceActor) const;

	void SetIsTemporarilyHiddenInEditor(ALevelInstance* LevelInstanceActor, bool bIsHidden);

	bool SetCurrent(ALevelInstance* LevelInstanceActor) const;
	bool IsCurrent(const ALevelInstance* LevelInstanceActor) const;
	ALevelInstance* CreateLevelInstanceFrom(const TArray<AActor*>& ActorsToMove, ELevelInstanceCreationType CreationType = ELevelInstanceCreationType::LevelInstance, UWorld* TemplateWorld = nullptr);
	bool MoveActorsToLevel(const TArray<AActor*>& ActorsToRemove, ULevel* DestinationLevel) const;
	bool MoveActorsTo(ALevelInstance* LevelInstanceActor, const TArray<AActor*>& ActorsToMove);
	bool BreakLevelInstance(ALevelInstance* LevelInstanceActor, uint32 Levels = 1);

	bool CanMoveActorToLevel(const AActor* Actor, FText* OutReason = nullptr) const;
	void DiscardEdits();
	void OnActorDeleted(AActor* Actor);
	ULevel* GetLevelInstanceLevel(const ALevelInstance* LevelInstanceActor) const;

	ALevelInstance* GetParentLevelInstance(const AActor* Actor) const;
	void BlockLoadLevelInstance(ALevelInstance* LevelInstanceActor);
#endif

private:
	void LoadLevelInstance(ALevelInstance* LevelInstanceActor);
	void UnloadLevelInstance(const FLevelInstanceID& LevelInstanceID);
	void ForEachActorInLevel(ULevel* Level, TFunctionRef<bool(AActor * LevelActor)> Operation) const;
	void ForEachLevelInstanceAncestors(AActor* Actor, TFunctionRef<bool(ALevelInstance*)> Operation) const;
	ALevelInstance* GetOwningLevelInstance(const ULevel* Level) const;
	FLevelInstanceID ComputeLevelInstanceID(ALevelInstance* LevelInstanceActor) const;
#if WITH_EDITOR
	void CommitChildrenLevelInstances(ALevelInstance* LevelInstanceActor);

	static bool ShouldIgnoreDirtyPackage(UPackage* DirtyPackage, const UWorld* EditingWorld);

	struct FLevelInstanceEdit
	{
		ULevelStreamingLevelInstanceEditor* LevelStreaming = nullptr;

		UWorld* GetEditWorld() const;
	};

	const FLevelInstanceEdit* GetLevelInstanceEdit(const ALevelInstance* LevelInstanceActor) const;
	bool IsLevelInstanceEditDirty(const FLevelInstanceEdit* LevelInstanceEdit) const;
	
	struct FLevelsToRemoveScope
	{
		~FLevelsToRemoveScope();

		TArray<ULevel*> Levels;
		bool bResetTrans = false;
	};
	
	friend ULevelStreamingLevelInstance;
	void RemoveLevelFromWorld(ULevel* Level, bool bResetTrans);
#endif


#if WITH_EDITORONLY_DATA
	// Optional scope to accelerate level unload by batching them
	TUniquePtr<FLevelsToRemoveScope> LevelsToRemoveScope;
	
	TMap<FName, FLevelInstanceEdit> LevelInstanceEdits;
#endif

	struct FLevelInstance
	{
		ULevelStreamingLevelInstance* LevelStreaming = nullptr;
	};

	TMap<ALevelInstance*, bool> LevelInstancesToLoadOrUpdate;
	TSet<FLevelInstanceID> LevelInstancesToUnload;
	TMap<FLevelInstanceID, FLevelInstance> LevelInstances;
	TMap<FLevelInstanceID, ALevelInstance*> RegisteredLevelInstances;
	FLevelInstanceID PendingLevelInstanceToEdit = InvalidLevelInstanceID;
};