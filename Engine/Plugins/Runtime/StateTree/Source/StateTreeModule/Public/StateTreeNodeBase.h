// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "StateTreeTypes.h"
#include "StateTreeNodeBase.generated.h"

/**
 * Base struct of StateTree Conditions, Evaluators, and Tasks.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeNodeBase
{
	GENERATED_BODY()

	FStateTreeNodeBase() = default;

	virtual ~FStateTreeNodeBase() {}

	/** @return Struct that represents the runtime data of the evaluator. */
	virtual const UStruct* GetInstanceDataType() const { return nullptr; };

	/**
	 * Called when the StateTree asset is linked. Allows to resolve references to other StateTree data.
	 * @see TStateTreeExternalDataHandle
	 * @see TStateTreeInstanceDataPropertyHandle
	 * @param Linker Reference to the linker
	 * @return true if linking succeeded. 
	 */
	virtual bool Link(FStateTreeLinker& Linker) { return true; }

	/** Name of the node. */
	UPROPERTY(EditDefaultsOnly, Category = "", meta=(EditCondition = "false", EditConditionHides))
	FName Name;

	/** Property binding copy batch handle. */
	UPROPERTY()
	FStateTreeHandle BindingsBatch = FStateTreeHandle::Invalid;

	/** The runtime data's data view index in the StateTreeExecutionContext, and source struct index in property binding. */
	UPROPERTY()
	uint16 DataViewIndex = 0;

	/** Index in runtime instance storage. */
	UPROPERTY()
	uint16 InstanceIndex = 0;

	/** True if the instance is an UObject. */
	UPROPERTY()
	uint8 bInstanceIsObject : 1;
};
