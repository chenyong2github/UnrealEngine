// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTypes.h"
#if WITH_EDITOR
#include "InstancedStruct.h"
#endif
#include "StateTreeConditionBase.generated.h"

#if WITH_EDITOR
struct IStateTreeBindingLookup;
struct FStateTreeEditorPropertyPath;
#endif
struct FStateTreeExecutionContext;

/**
 * Base struct for all conditions.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeConditionBase
{
	GENERATED_BODY()

	virtual ~FStateTreeConditionBase() {}

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

#if WITH_EDITOR
	/** @return Rich text description of the condition. */
	virtual FText GetDescription(const FGuid& ID, FConstStructView LayoutData, const IStateTreeBindingLookup& BindingLookup) const { return FText::GetEmpty(); }
	/**
	 * Called when binding of any of the properties in the condition changes.
	 * @param SourcePath Source path of the new binding.
	 * @param TargetPath Target path of the new binding (the property in the condition).
	 * @param BindingLookup Reference to binding lookup which can be used to reason about property paths.
	 */
	virtual void OnBindingChanged(const FGuid& ID, FStructView LayoutData, const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup) {}
#endif
	/** @return True if the condition passes. */
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const { return false; }

	/** Property binding copy batch handle. */
	UPROPERTY()
	FStateTreeHandle BindingsBatch = FStateTreeHandle::Invalid;

	/** The runtime data's data view index in the StateTreeExecutionContext, and source struct index in property binding. */
	UPROPERTY()
	uint16 DataViewIndex = 0;

	UPROPERTY()
	uint16 InstanceIndex = 0;

	UPROPERTY()
	uint8 bInstanceIsObject : 1;
};
