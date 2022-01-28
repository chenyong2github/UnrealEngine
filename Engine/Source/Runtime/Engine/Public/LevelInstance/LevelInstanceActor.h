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
	// Deprecated exists only to avoid breaking Actor Desc serialization
	Embedded_Deprecated UMETA(Hidden),
	// Default behavior is to move Level Instance level actors to the main world partition using World Partition clustering rules
	Partitioned,
	// Behavior only supported through Conversion Commandlet or on non OFPA Level Instances
	LevelStreaming UMETA(Hidden)
};

UCLASS()
class ENGINE_API ALevelInstance : public AActor
{
	GENERATED_UCLASS_BODY()

protected:
#if WITH_EDITORONLY_DATA
	/** Level LevelInstance */
	UPROPERTY(EditAnywhere, Category = Level, meta = (NoCreate, DisplayName="Level"))
	TSoftObjectPtr<UWorld> WorldAsset;
#endif

	UPROPERTY()
	TSoftObjectPtr<UWorld> CookedWorldAsset;

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	ELevelInstanceRuntimeBehavior DesiredRuntimeBehavior;
#endif

	void LoadLevelInstance();
	void UnloadLevelInstance();
	virtual bool SupportsLoading() const;
	bool IsLevelInstancePathValid() const;
	const TSoftObjectPtr<UWorld>& GetWorldAsset() const;
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
	virtual void PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation) override;
	virtual void PostLoad() override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditImport() override;

	// AActor overrides
	virtual void CheckForErrors() override;
	virtual TUniquePtr<class FWorldPartitionActorDesc> CreateClassActorDesc() const override;
	virtual bool CanDeleteSelectedActor(FText& OutReason) const override;
	virtual void SetIsTemporarilyHiddenInEditor(bool bIsHidden) override;
	virtual bool SetIsHiddenEdLayer(bool bIsHiddenEdLayer) override;
	virtual void EditorGetUnderlyingActors(TSet<AActor*>& OutUnderlyingActors) const override;
	virtual void PushSelectionToProxies() override;
	virtual void PushLevelInstanceEditingStateToProxies(bool bInEditingState) override;
	virtual FBox GetComponentsBoundingBox(bool bNonColliding = false, bool bIncludeFromChildActors = false) const override;
	virtual FBox GetStreamingBounds() const override;
	virtual bool IsLockLocation() const override;
	virtual ELevelInstanceRuntimeBehavior GetDesiredRuntimeBehavior() const { return DesiredRuntimeBehavior; }
	virtual ELevelInstanceRuntimeBehavior GetDefaultRuntimeBehavior() const { return ELevelInstanceRuntimeBehavior::Partitioned; }

	bool CanEdit(FText* OutReason = nullptr) const;
	bool CanCommit(FText* OutReason = nullptr) const;
	bool CanDiscard(FText* OutReason = nullptr) const;
	bool IsEditing() const;
	ULevel* GetLoadedLevel() const;
	bool HasChildEdit() const;
	void Edit(AActor* ContextActor = nullptr);
	void Commit();
	void Discard();
	bool HasDirtyChildren() const;
	bool IsDirty() const;
	bool SetCurrent();
	bool IsCurrent() const;
	bool IsLoaded() const;
	void OnLevelInstanceLoaded();
	FString GetWorldAssetPackage() const;
	bool SetWorldAsset(TSoftObjectPtr<UWorld> WorldAsset);
	bool CheckForLoop(TSoftObjectPtr<UWorld> WorldAsset, TArray<TPair<FText, TSoftObjectPtr<UWorld>>>* LoopInfo = nullptr, const ALevelInstance** LoopStart = nullptr) const;
	AActor* FindEditorInstanceActor() const;

	virtual void OnWorldAssetChanged() { UpdateFromLevel(); }
	virtual void OnEdit() {}
	virtual void OnEditChild() {}
	virtual void OnCommit(bool bChanged) {}
	virtual void OnCommitChild(bool bChanged) {}
	virtual void UpdateFromLevel();
		
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelInstanceActorPostLoad, ALevelInstance*);
	static FOnLevelInstanceActorPostLoad OnLevelInstanceActorPostLoad;
#endif

private:

#if WITH_EDITOR
	void PostEditUndoInternal();
	bool CanSetValue(TSoftObjectPtr<UWorld> LevelInstance, FString* Reason = nullptr) const;
	
	TSoftObjectPtr<UWorld> CachedWorldAsset;
	FLevelInstanceID CachedLevelInstanceID;
	bool bCachedIsTemporarilyHiddenInEditor;
	bool bGuardLoadUnload;
#else
	// This Guid is used to compute the LevelInstanceID. Because in non-editor build we don't have an ActorGuid, we store it at cook time.
	FGuid LevelInstanceActorGuid;
#endif

	FLevelInstanceID LevelInstanceID;
};

DEFINE_ACTORDESC_TYPE(ALevelInstance, FLevelInstanceActorDesc);
