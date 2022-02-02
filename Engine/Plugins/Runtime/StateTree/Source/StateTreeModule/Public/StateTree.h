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
	~UStateTree();

	/** @return Script Struct that can be used to instantiate the runtime storage */
	const UScriptStruct* GetInstanceStorageStruct() const { return InstanceStorageStruct; }

	/** @return Instance of the runtime storage that contains the default values */
	const FInstancedStruct& GetInstanceStorageDefaultValue() const { return InstanceStorageDefaultValue; }

	/** @return Number of runtime data (Evaluators, Tasks, Conditions) in the runtime storage. */
	int32 GetNumInstances() const { return Instances.Num(); }

	/** @return Number of data views required for StateTree execution (Evaluators, Tasks, Conditions, External data). */
	int32 GetNumDataViews() const { return NumDataViews; }

	/** @return Base index in data views for external data. */
	int32 GetExternalDataBaseIndex() const { return ExternalDataBaseIndex; }

	/** @return List of external data required by the state tree */
	TConstArrayView<FStateTreeExternalDataDesc> GetExternalDataDescs() const { return ExternalDataDescs; }

	/** @return Schema describing which inputs, evaluators, and tasks a StateTree can contain */
	const UStateTreeSchema* GetSchema() const { return Schema; }
	void SetSchema(UStateTreeSchema* InSchema) { Schema = InSchema; }

	/** @return true is the tree asset is considered valid (e.g. at least one state) */
	bool IsValidStateTree() const;

	void ResolvePropertyPaths();

#if WITH_EDITOR
	void OnPIEStarted(const bool bIsSimulating);
	
	/** Resets the baked data to empty. */
	void ResetBaked();
#endif

#if WITH_EDITORONLY_DATA
	// Edit time data for the StateTree, instance of UStateTreeEditorData
	UPROPERTY()
	TObjectPtr<UObject> EditorData;

	// Hash of the editor data from last compile.
	UPROPERTY()
	uint32 LastCompiledEditorDataHash = 0;
#endif

protected:
	virtual void BeginDestroy() override;
	virtual void PostLoad() override;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif

	/** Initializes the types and default values related to StateTree runtime storage */
	void InitInstanceStorageType();

	/** Resolved references between data in the StateTree. */
	void Link();

private:

	// Properties

	UPROPERTY(EditDefaultsOnly, Category = Common, Instanced)
	TObjectPtr<UStateTreeSchema> Schema = nullptr;

	/** Evaluators, Tasks, and Condition items */
	UPROPERTY()
	TArray<FInstancedStruct> Items;

	/** Evaluators, Tasks, and Conditions runtime data. */
	UPROPERTY()
	TArray<FInstancedStruct> Instances;

	/** Blueprint based Evaluators, Tasks, and Conditions runtime data. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> InstanceObjects;

	/** Script Struct that can be used to instantiate the runtime storage */
	UPROPERTY()
	TObjectPtr<UScriptStruct> InstanceStorageStruct;

	/** Offsets into the runtime type to quickly get a struct view to a specific Task or Evaluator */
	TArray<FStateTreeInstanceStorageOffset> InstanceStorageOffsets;

	/** Instance of the runtime storage that contains the default values. */
	UPROPERTY()
	FInstancedStruct InstanceStorageDefaultValue;

	/** List of external data required by the state tree, creating during linking. */
	UPROPERTY(Transient)
	TArray<FStateTreeExternalDataDesc> ExternalDataDescs;

	UPROPERTY(Transient)
	int32 NumDataViews = 0;

	UPROPERTY(Transient)
	int32 ExternalDataBaseIndex = 0;

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







