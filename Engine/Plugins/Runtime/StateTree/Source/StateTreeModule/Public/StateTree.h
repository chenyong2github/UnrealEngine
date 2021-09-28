// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StateTreeTypes.h"
#include "StateTreeSchema.h"
#include "StateTreeCondition.h"
#include "StateTreeVariableLayout.h"
#include "StateTreeParameterLayout.h"
#include "StateTreeConstantStorage.h"
#include "InstancedStruct.h"
#include "StateTreePropertyBindings.h"
#include "StateTree.generated.h"

class UStateTreeEvaluatorBase;
class UStateTreeTaskBase;

/**
 * StateTree asset. Contains the StateTree definition in both editor and runtime (baked) formats.
 */
UCLASS(BlueprintType)
class STATETREEMODULE_API UStateTree : public UDataAsset
{
	GENERATED_BODY()

public:

	UStateTree(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	const FStateTreeVariableLayout& GetInputParameterLayout() const { return InputParameter; }

	/** @return Script Struct that can be used to instantiate the runtime storage */
	const UScriptStruct* GetRuntimeStorageStruct() const { return RuntimeStorageStruct; }

	/** @return Instance of the runtime storage that contains the default values */
	const FInstancedStruct& GetRuntimeStorageDefaultValue() const { return RuntimeStorageDefaultValue; }

	/** @return List of external items required by the state tree */
	TConstArrayView<FStateTreeExternalItemDesc> GetExternalItems() const { return ExternalItems; }

	/** @return Schema describing which inputs, evaluators, and tasks a StateTree can contain */
	const UStateTreeSchema* GetSchema() const { return Schema; }
	void SetSchema(UStateTreeSchema* InSchema) { Schema = InSchema; }

	/** @return true is the tree asset is considered valid (e.g. at least one state) */
	bool IsValidStateTree() const;

	// STATETREE_V2
	void ResolvePropertyPaths();
	bool IsV2() const { return Schema ? Schema->IsV2() : false; }
	// ~STATETREE_V2

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

	/** @return Number of items (Evaluators & Tasks) in the runtime storage. */
	int32 GetRuntimeStorageItemCount() const { return RuntimeStorageOffsets.Num(); }

private:

	// Properties

	UPROPERTY(EditDefaultsOnly, Category = Common, Instanced)
	UStateTreeSchema* Schema = nullptr;

	// TODO: find a nice way to combine this with Parameters. The data is almost the same, but the semantics different:
	// InputParameter defines layout with offsets, while Parameters defines how they connect to Variables.
	// This could be editor only, maybe.
	UPROPERTY(EditDefaultsOnly, Category = InputParameters)
	FStateTreeVariableLayout InputParameter;

	// Baked data
	UPROPERTY()
	FStateTreeVariableLayout Variables;

	UPROPERTY()
	FStateTreeConstantStorage Constants;

	UPROPERTY()
	TArray<UStateTreeTaskBase*> Tasks;

	UPROPERTY()
	TArray<UStateTreeEvaluatorBase*> Evaluators;

// STATETREE_V2
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
	TArray<FInstancedStruct> Conditions2;

	/** List of external items required by the state tree */
	UPROPERTY()
	TArray<FStateTreeExternalItemDesc> ExternalItems;

	UPROPERTY()
	FStateTreePropertyBindings PropertyBindings;
	
// ~STATETREE_V2

	UPROPERTY()
	TArray<FBakedStateTreeState> States;

	UPROPERTY()
	TArray<FStateTreeCondition> Conditions;

	UPROPERTY()
	TArray<FBakedStateTransition> Transitions;

	UPROPERTY()
	FStateTreeParameterLayout Parameters;

	friend struct FStateTreeInstance;
	friend struct FStateTreeExecutionContext;
#if WITH_EDITORONLY_DATA
	friend struct FStateTreeBaker;
#endif
};







