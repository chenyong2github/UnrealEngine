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

#if WITH_EDITOR
	static FName GetPackedComponentTag();

	virtual void OnWorldAssetChanged() override;
	virtual void OnWorldAssetSaved() override;
	virtual void OnCommit() override;
	virtual void OnCommitChild() override;
	virtual void OnEdit() override;
	virtual void OnEditChild() override;

	virtual bool CanEditChange(const FProperty* InProperty) const override;

	virtual bool IsHiddenEd() const override;

	void DestroyPackedComponents();
	void GetPackedComponents(TArray<UActorComponent*>& OutPackedComponents) const;

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
	TArray<TSoftObjectPtr<UWorld>> PackDependencies;

private:
	UPROPERTY(NonTransactional)
	bool bEditing = false;

	UPROPERTY(NonTransactional)
	int32 ChildEditing = 0;
#endif
};