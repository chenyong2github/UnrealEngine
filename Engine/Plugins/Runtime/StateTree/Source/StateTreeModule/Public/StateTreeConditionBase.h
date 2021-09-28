// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "StateTreeTypes.h"
#include "StateTreeConditionBase.generated.h"

struct IStateTreeBindingLookup;
struct FStateTreeEditorPropertyPath;

/**
 * Base struct for all conditions.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeConditionBase
{
	GENERATED_BODY()

	FStateTreeConditionBase()
	{
	}

	virtual ~FStateTreeConditionBase() {}

#if WITH_EDITOR
	/** @return Rich text description of the condition. */
	virtual FText GetDescription(const IStateTreeBindingLookup& BindingLookup) const { return FText::GetEmpty(); }
	/**
	 * Called when binding of any of the properties in the condition changes.
	 * @param SourcePath Source path of the new binding.
	 * @param TargetPath Target path of the new binding (the property in the condition).
	 * @param BindingLookup Reference to binding lookup which can be used to reason about property paths.
	 */
	virtual void OnBindingChanged(const FStateTreeEditorPropertyPath& SourcePath, const FStateTreeEditorPropertyPath& TargetPath, const IStateTreeBindingLookup& BindingLookup) {}
#endif
	/** @return True if the condition passes. */
	virtual bool TestCondition() const { return false; }

#if WITH_EDITORONLY_DATA
	/** Edit time struct ID for property binding. */
	UPROPERTY(EditDefaultsOnly, Category = Evaluator, meta = (IgnoreForMemberInitializationTest, EditCondition = "false", EditConditionHides))	// Hack, we want the ID to be found as IPropertyHandle, but do not want to display it.
	FGuid ID;
#endif

	/** Property binding copy batch handle. */
	UPROPERTY()
	FStateTreeHandle BindingsBatch = FStateTreeHandle::Invalid;
};
