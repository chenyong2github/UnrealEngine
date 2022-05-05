// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

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

	/** @return Default value for the instance data. */
	const FStateTreeInstanceData& GetInstanceDataDefaultValue() const { return InstanceDataDefaultValue; }
	
	/** @return Number of runtime data (Evaluators, Tasks, Conditions) in the runtime storage. */
	int32 GetNumInstances() const { return Instances.Num(); }

	/** @return Number of data views required for StateTree execution (Evaluators, Tasks, Conditions, External data). */
	int32 GetNumDataViews() const { return NumDataViews; }

	/** @return List of external data required by the state tree */
	TConstArrayView<FStateTreeExternalDataDesc> GetExternalDataDescs() const { return ExternalDataDescs; }

	/** @return List of named external data enforced by the schema that must be provided through the execution context. */
	TConstArrayView<FStateTreeExternalDataDesc> GetNamedExternalDataDescs() const { return NamedExternalDataDescs; }

	/** @return List of default parameters of the state tree. Default parameter values can be overridden at runtime by the execution context. */
	const FInstancedPropertyBag& GetDefaultParameters() const { return Parameters; }

	/** @return true if the tree asset can be used at runtime. */
	bool IsReadyToRun() const;

#if WITH_EDITOR
	/** Resets the compiled data to empty. */
	void ResetCompiled();
#endif

#if WITH_EDITORONLY_DATA
	/** Edit time data for the StateTree, instance of UStateTreeEditorData */
	UPROPERTY()
	TObjectPtr<UObject> EditorData;

	/** Hash of the editor data from last compile. */
	UPROPERTY()
	uint32 LastCompiledEditorDataHash = 0;
#endif

protected:
	
	/**
	 * Resolves references between data in the StateTree.
	 * @return true if all references to internal and external data are resolved properly, false otherwise.
	 */
	[[nodiscard]] bool Link();

	virtual void PostLoad() override;
	virtual void Serialize(FStructuredArchiveRecord Record) override;
	
#if WITH_EDITOR
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
#endif

private:

	// Properties

	UPROPERTY(Instanced)
	TObjectPtr<UStateTreeSchema> Schema = nullptr;

	/**
	 * Parameters that could be used for bindings within the Tree.
	 * Default values are stored within the asset but StateTreeReference can be used to parameterized the tree.
	 * @see FStateTreeReference
	 */
	UPROPERTY()
	FInstancedPropertyBag Parameters;

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

	/** List of external data required by the state tree, created during linking. */
	UPROPERTY(Transient)
	TArray<FStateTreeExternalDataDesc> ExternalDataDescs;

	/** List of names external data enforced by the schema, created at compilation. */
	UPROPERTY(Transient)
	TArray<FStateTreeExternalDataDesc> NamedExternalDataDescs;
	
	UPROPERTY(Transient)
	int32 NumDataViews = 0;

	UPROPERTY(Transient)
	int32 ExternalDataBaseIndex = 0;

	/** Data view index of the tree parameters */
	UPROPERTY()
	int32 DefaultParametersDataViewIndex = INDEX_NONE;
	
	UPROPERTY()
	FStateTreePropertyBindings PropertyBindings;

	UPROPERTY()
	TArray<FCompactStateTreeState> States;

	UPROPERTY()
	TArray<FCompactStateTransition> Transitions;

	friend struct FStateTreeInstance;
	friend struct FStateTreeExecutionContext;
#if WITH_EDITORONLY_DATA
	friend struct FStateTreeCompiler;
#endif
};

