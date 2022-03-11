// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StateTreeTypes.h"
#include "StateTreeSchema.h"
#include "InstancedStruct.h"
#include "StateTreePropertyBindings.h"
#include "StateTreeInstanceData.h"
#include "StateTree.generated.h"

/**
 * StateTree asset. Contains the StateTree definition in both editor and runtime (baked) formats.
 */
UCLASS(BlueprintType)
class STATETREEMODULE_API UStateTree : public UDataAsset
{
	GENERATED_BODY()

public:

	/** @return Size required for the instance data in bytes. */
	int32 GetInstanceDataSize() const;
	
	/** @return Number of runtime data (Evaluators, Tasks, Conditions) in the runtime storage. */
	int32 GetNumInstances() const { return Instances.Num(); }

	/** @return Number of data views required for StateTree execution (Evaluators, Tasks, Conditions, External data). */
	int32 GetNumDataViews() const { return NumDataViews; }

	/** @return List of external data required by the state tree */
	TConstArrayView<FStateTreeExternalDataDesc> GetExternalDataDescs() const { return ExternalDataDescs; }

	/** @return Schema describing which inputs, evaluators, and tasks a StateTree can contain */
	const UStateTreeSchema* GetSchema() const { return Schema; }
	void SetSchema(UStateTreeSchema* InSchema) { Schema = InSchema; }

	/** @return true is the tree asset is to be used at runtime. */
	bool IsReadyToRun() const;

#if WITH_EDITOR
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
	
	/** Resolved references between data in the StateTree. */
	void Link();

	virtual void PostLoad() override;
	
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif

private:

	// Properties

	UPROPERTY(EditDefaultsOnly, Category = Common, Instanced)
	TObjectPtr<UStateTreeSchema> Schema = nullptr;

	/** Evaluators, Tasks, and Condition items */
	UPROPERTY()
	TArray<FInstancedStruct> Nodes;

	/** Evaluators, Tasks, and Conditions runtime data. */
	UPROPERTY()
	TArray<FInstancedStruct> Instances;

	/** Blueprint based Evaluators, Tasks, and Conditions runtime data. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> InstanceObjects;

	UPROPERTY(Transient)
	FStateTreeInstanceData InstanceDataDefaultValue;

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







