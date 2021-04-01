// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Containers/Array.h"
#include "LevelInstance/LevelInstanceTypes.h"
#include "WorldPartition/WorldPartitionActorDesc.h"
#include "LevelInstanceActor.generated.h"

UENUM()
enum class ELevelInstanceRuntimeBehavior : uint8
{
	None UMETA(Hidden),
	Embedded UMETA(ToolTip="Level Instance is discarded at runtime and all its actors are part of the same runtime World Partition cell"),
	Partitioned UMETA(Hidden), 
	LevelStreaming UMETA(ToolTip="Level Instance is streamed like a regular level"),
};

UCLASS()
class ENGINE_API ALevelInstance : public AActor
{
	GENERATED_UCLASS_BODY()

protected:
	/** Level LevelInstance */
	UPROPERTY(EditAnywhere, Category = LevelInstance, meta = (NoCreate))
	TSoftObjectPtr<UWorld> WorldAsset;

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Runtime)
	ELevelInstanceRuntimeBehavior DesiredRuntimeBehavior;
#endif

	void LoadLevelInstance();
	void UnloadLevelInstance();
	virtual bool SupportsLoading() const;
	bool IsLevelInstancePathValid() const;
	const TSoftObjectPtr<UWorld>& GetWorldAsset() const { return WorldAsset; }
	class ULevelInstanceSubsystem* GetLevelInstanceSubsystem() const;
	
	const FLevelInstanceID& GetLevelInstanceID() const;
	bool HasValidLevelInstanceID() const;

	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void Serialize(FArchive& Ar) override;

	const FGuid& GetLevelInstanceActorGuid() const;
#if WITH_EDITOR
	// UObject overrides
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	virtual void PostLoad() override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditImport() override;

	// AActor overrides
	virtual void CheckForErrors() override;
	virtual EActorGridPlacement GetDefaultGridPlacement() const override { return EActorGridPlacement::Bounds; }
	virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;
	virtual bool CanDeleteSelectedActor(FText& OutReason) const override;
	virtual void SetIsTemporarilyHiddenInEditor(bool bIsHidden) override;
	virtual void EditorGetUnderlyingActors(TSet<AActor*>& OutUnderlyingActors) const override;
	virtual void PushSelectionToProxies() override;
	virtual void PushLevelInstanceEditingStateToProxies(bool bInEditingState) override;
	virtual FBox GetComponentsBoundingBox(bool bNonColliding = false, bool bIncludeFromChildActors = false) const override;
	virtual void GetActorLocationBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors = false) const override;
	virtual bool IsLockLocation() const override;
	virtual ELevelInstanceRuntimeBehavior GetDesiredRuntimeBehavior() const { return DesiredRuntimeBehavior; }
	virtual ELevelInstanceRuntimeBehavior GetDefaultRuntimeBehavior() const { return ELevelInstanceRuntimeBehavior::Embedded; }
	virtual bool IsHLODRelevant() const override { return true; }

	bool CanEdit(FText* OutReason = nullptr) const;
	bool CanCommit(FText* OutReason = nullptr) const;
	bool IsEditing() const;
	bool HasEditingChildren() const;
	void Edit(AActor* ContextActor = nullptr);
	void Commit();
	void Discard();
	void SaveAs();
	bool HasDirtyChildren() const;
	bool IsDirty() const;
	bool SetCurrent();
	bool IsCurrent() const;
	bool IsLoaded() const;
	void OnLevelInstanceLoaded();
	FString GetWorldAssetPackage() const;
	void UpdateLevelInstance();
	bool SetWorldAsset(TSoftObjectPtr<UWorld> WorldAsset);
	bool CheckForLoop(TSoftObjectPtr<UWorld> WorldAsset, TArray<TPair<FText, TSoftObjectPtr<UWorld>>>* LoopInfo = nullptr, const ALevelInstance** LoopStart = nullptr) const;
	AActor* FindEditorInstanceActor() const;

	virtual void OnWorldAssetChanged() { UpdateLevelInstance(); }
	virtual void OnWorldAssetSaved(bool bPromptForSave) {}
	virtual void OnEdit();
	virtual void OnEditChild() {}
	virtual void OnCommit();
	virtual void OnCommitChild(bool bChanged) {}
		
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelInstanceActorPostLoad, ALevelInstance*);
	static FOnLevelInstanceActorPostLoad OnLevelInstanceActorPostLoad;
#endif

private:

#if WITH_EDITOR
	bool CanSetValue(TSoftObjectPtr<UWorld> LevelInstance, FString* Reason = nullptr) const;
	
	TSoftObjectPtr<UWorld> CachedWorldAsset;
	FLevelInstanceID CachedLevelInstanceID;
	bool bCachedIsTemporarilyHiddenInEditor;
	bool bGuardLoadUnload;
	bool bEditLockLocation;
#else
	// This Guid is used to compute the LevelInstanceID. Because in non-editor build we don't have an ActorGuid, we store it at cook time.
	FGuid LevelInstanceActorGuid;
#endif

	FLevelInstanceID LevelInstanceID;
};

DEFINE_ACTORDESC_TYPE(ALevelInstance, FLevelInstanceActorDesc);
