// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StateTreeTypes.h"
#include "StateTreeSchema.h"
#include "InstancedStruct.h"
#include "StateTreePropertyBindings.h"
#include "StateTree.generated.h"

/**
 * StateTree asset. Contains the StateTree definition in both editor and runtime (baked) formats.
 */
UCLASS(BlueprintType)
class STATETREEMODULE_API UStateTree : public UDataAsset
{
	GENERATED_BODY()

public:

	UStateTree(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** @return Script Struct that can be used to instantiate the runtime storage */
	const UScriptStruct* GetRuntimeStorageStruct() const { return RuntimeStorageStruct; }

	/** @return Instance of the runtime storage that contains the default values */
	const FInstancedStruct& GetRuntimeStorageDefaultValue() const { return RuntimeStorageDefaultValue; }

	/** @return Number of items (Evaluators & Tasks) in the runtime storage. */
	int32 GetRuntimeStorageItemCount() const { return RuntimeStorageOffsets.Num(); }

	/** @return Number of linked items (Evaluators, Tasks, external items). */
	int32 GetLinkedItemCount() const { return NumLinkedItems; }

	/** @return Base index for external items. */
	int32 GetExternalItemBaseIndex() const { return ExternalItemBaseIndex; }

	/** @return List of external items required by the state tree */
	TConstArrayView<FStateTreeExternalItemDesc> GetExternalItems() const { return ExternalItems; }

	/** @return Schema describing which inputs, evaluators, and tasks a StateTree can contain */
	const UStateTreeSchema* GetSchema() const { return Schema; }
	void SetSchema(UStateTreeSchema* InSchema) { Schema = InSchema; }

	/** @return true is the tree asset is considered valid (e.g. at least one state) */
	bool IsValidStateTree() const;

	void ResolvePropertyPaths();

#if WITH_EDITOR
	/** Resets the baked data to empty. */
	void ResetBaked();
#endif

#if WITH_EDITORONLY_DATA
	// Edit time data for the StateTree, instance of UStateTreeEditorData
	UPROPERTY()
	UObject* EditorData;
#endif

protected:
	virtual void BeginDestroy() override;
	virtual void PostLoad() override;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif

	/** Initializes the types and default values related to StateTree runtime storage */
	void InitRuntimeStorage();

	/** Resolved references between data in the StateTree. */
	void Link();

private:

	// Properties

	UPROPERTY(EditDefaultsOnly, Category = Common, Instanced)
	UStateTreeSchema* Schema = nullptr;

	/** Evaluators and Tasks that require runtime state */
	UPROPERTY()
	TArray<FInstancedStruct> RuntimeStorageItems;

	/** Script Struct that can be used to instantiate the runtime storage */
	UPROPERTY()
	UScriptStruct* RuntimeStorageStruct;

	/** Offsets into the runtime type to quickly get a struct view to a specific Task or Evaluator */
	TArray<FStateTreeRuntimeStorageItemOffset> RuntimeStorageOffsets;

	/** Instance of the runtime storage that contains the default values. */
	UPROPERTY()
	FInstancedStruct RuntimeStorageDefaultValue;

	UPROPERTY(meta = (BaseStruct = "StateTreeConditionBase", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	/** List of external items required by the state tree, creating during linking. */
	UPROPERTY(Transient)
	TArray<FStateTreeExternalItemDesc> ExternalItems;

	UPROPERTY(Transient)
	int32 NumLinkedItems = 0;

	UPROPERTY(Transient)
	int32 ExternalItemBaseIndex = 0;

	UPROPERTY()
	FStateTreePropertyBindings PropertyBindings;

	UPROPERTY()
	TArray<FBakedStateTreeState> States;

	UPROPERTY()
	TArray<FBakedStateTransition> Transitions;

	friend struct FStateTreeInstance;
	friend struct FStateTreeExecutionContext;
#if WITH_EDITORONLY_DATA
	friend struct FStateTreeBaker;
#endif
};







