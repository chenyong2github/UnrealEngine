// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Containers/Array.h"
#include "Foundation/FoundationTypes.h"
#include "FoundationActor.generated.h"

UCLASS()
class ENGINE_API AFoundationActor : public AActor
{
	GENERATED_UCLASS_BODY()

protected:
	/** Level Foundation */
	UPROPERTY(EditAnywhere, Category = Foundation)
	TSoftObjectPtr<UWorld> Foundation;
public:

	void LoadFoundation();
	void UnloadFoundation();
	bool IsFoundationPathValid() const;
	const TSoftObjectPtr<UWorld>& GetFoundation() const { return Foundation; }
	class UFoundationSubsystem* GetFoundationSubsystem() const;
	
	const FFoundationID& GetFoundationID() const;
	bool HasValidFoundationID() const;

	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void Serialize(FArchive& Ar) override;

	const FGuid& GetFoundationActorGuid() const;
#if WITH_EDITOR
	// UObject overrides
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	virtual void PostLoad() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditImport() override;

	// AActor overrides
	virtual void CheckForErrors() override;
	virtual EActorGridPlacement GetDefaultGridPlacement() const override { return EActorGridPlacement::Bounds; }
	virtual bool CanDeleteSelectedActor(FText& OutReason) const override;
	virtual void SetIsTemporarilyHiddenInEditor(bool bIsHidden) override;
	virtual void EditorGetUnderlyingActors(TSet<AActor*>& OutUnderlyingActors) override;
	virtual void PushSelectionToProxies() override;
	virtual FBox GetComponentsBoundingBox(bool bNonColliding = false, bool bIncludeFromChildActors = false) const override;
	virtual void GetActorLocationBounds(bool bOnlyCollidingComponents, FVector& Origin, FVector& BoxExtent, bool bIncludeFromChildActors = false) const override;

	bool CanEdit(FText* OutReason = nullptr) const;
	bool CanCommit(FText* OutReason = nullptr) const;
	bool IsEditing() const;
	void Edit(AActor* ContextActor = nullptr);
	void Commit();
	void Discard();
	void SaveAs();
	bool HasDirtyChildren() const;
	bool IsDirty() const;
	bool SetCurrent();
	bool IsCurrent() const;
	bool IsLoaded() const;
	void OnFoundationLoaded();
	FString GetFoundationPackage() const;
	void UpdateFoundation();
	bool SetFoundation(TSoftObjectPtr<UWorld> Foundation);
	bool CheckForLoop(TSoftObjectPtr<UWorld> Foundation, TArray<TPair<FText, TSoftObjectPtr<UWorld>>>* LoopInfo = nullptr, const AFoundationActor** LoopStart = nullptr) const;
	AActor* FindEditorInstanceActor() const;

	DECLARE_MULTICAST_DELEGATE_OneParam(FOnFoundationActorPostLoad, AFoundationActor*);
	static FOnFoundationActorPostLoad OnFoundationActorPostLoad;
#endif

private:

#if WITH_EDITOR
	bool CanSetValue(TSoftObjectPtr<UWorld> Foundation, FString* Reason = nullptr) const;
	
	TSoftObjectPtr<UWorld> CachedFoundation;
	FFoundationID CachedFoundationID;
	bool bCachedIsTemporarilyHiddenInEditor;
	bool bGuardLoadUnload;
#else
	// This Guid is used to compute the FoundationID. Because in non-editor build we don't have an ActorGuid, we store it at cook time.
	FGuid FoundationActorGuid;
#endif

	FFoundationID FoundationID;
};