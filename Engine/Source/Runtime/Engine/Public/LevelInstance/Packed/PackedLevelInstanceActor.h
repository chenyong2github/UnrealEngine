// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "LevelInstance/LevelInstanceActor.h"

#if WITH_EDITOR
#include "Kismet2/ComponentEditorUtils.h"
#endif

#include "PackedLevelInstanceActor.generated.h"

class UInstancedStaticMeshComponent;
class UBlueprint;

/**
 * APackedLevelInstance is the result of packing the source level (WorldAsset base class property) into a single actor. See FPackedLevelInstanceBuilder.
 * 
 * Currently supported source components:
 * Packer FLevelInstanceISMPacker : UStaticMeshComponent/UInstancedStaticMeshComponent/UHierarchicalInstancedStaticMeshComponent
 * Packer FRecursiveLevelInstancePacker : Allows packing recursive LevelInstances
 * 
 * Other components are unsupported and will result in an incomplete APackedLevelInstance. In this case using a regular ALevelInstance is recommended.
 */
UCLASS()
class ENGINE_API APackedLevelInstance : public ALevelInstance
{
	GENERATED_UCLASS_BODY()

public:
	virtual bool SupportsLoading() const override;

	virtual void Serialize(FArchive& Ar) override;
#if WITH_EDITOR
	static FName GetPackedComponentTag();

	virtual void PostLoad() override;
	virtual void OnWorldAssetChanged() override;
	virtual void OnWorldAssetSaved(bool bPromptForSave) override;
	virtual void OnCommit() override;
	virtual void OnCommitChild(bool bChanged) override;
	virtual void OnEdit() override;
	virtual void OnEditChild() override;

	virtual bool CanEditChange(const FProperty* InProperty) const override;
		
	void SetPackedVersion(const FGuid& Version) { PackedVersion = Version; }

	// AActor overrides
	virtual EActorGridPlacement GetDefaultGridPlacement() const override { return EActorGridPlacement::None; }

	virtual bool IsHiddenEd() const override;

	void DestroyPackedComponents();
	void GetPackedComponents(TArray<UActorComponent*>& OutPackedComponents) const;

	virtual ELevelInstanceRuntimeBehavior GetDefaultRuntimeBehavior() const override { return ELevelInstanceRuntimeBehavior::None; }

	virtual void RerunConstructionScripts() override;

	template<class T>
	T* AddPackedComponent()
	{
		Modify();
		FName NewComponentName = *FComponentEditorUtils::GenerateValidVariableName(T::StaticClass(), this);
		T* NewComponent = NewObject<T>(this, NewComponentName, RF_Transactional);
		AddInstanceComponent(NewComponent);
		NewComponent->ComponentTags.Add(GetPackedComponentTag());
		return NewComponent;
	}
#endif

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = Pack, meta = (DisplayName = "ISM Component Class"))
	TSubclassOf<UInstancedStaticMeshComponent> ISMComponentClass;

	UPROPERTY(VisibleAnywhere, Category = Pack)
	TSoftObjectPtr<UBlueprint> BlueprintAsset;

	UPROPERTY()
	TArray<TSoftObjectPtr<UBlueprint>> PackedBPDependencies;

private:
	UPROPERTY(NonTransactional)
	bool bEditing;

	UPROPERTY(NonTransactional)
	int32 ChildEditing;

	UPROPERTY(NonTransactional)
	bool bChildChanged;

	UPROPERTY()
	FGuid PackedVersion;
#endif
};