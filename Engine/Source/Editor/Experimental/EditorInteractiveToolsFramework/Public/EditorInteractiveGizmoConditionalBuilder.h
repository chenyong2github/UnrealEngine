// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveGizmoBuilder.h"
#include "ToolContextInterfaces.h"
#include "EditorInteractiveGizmoConditionalBuilder.generated.h"

/**
 * FEditorGizmoTypePriority is used to establish relative priority between conditional 
 * gizmo builders. It is up to the gizmo manager to determine how the priority is used. 
 * In the EditorInteractiveGizmoManager, if more than one gizmo builder returns true 
 * from SatsifiesCondition(), the gizmo builder with highest priority will be used. If 
 * there are multiple builders the highest priority, multiple gizmos will be built.
 */
struct EDITORINTERACTIVETOOLSFRAMEWORK_API FEditorGizmoTypePriority
{
	static constexpr int DEFAULT_GIZMO_TYPE_PRIORITY = 50;

	/** Constant priority value */
	int Priority;

	FEditorGizmoTypePriority(int InPriority = DEFAULT_GIZMO_TYPE_PRIORITY)
	{
		Priority = InPriority;
	}

	/** @return a priority lower than this priority */
	FEditorGizmoTypePriority MakeLower(int DeltaAmount = 1) const
	{
		return FEditorGizmoTypePriority(Priority + DeltaAmount);
	}

	/** @return a priority higher than this priority */
	FEditorGizmoTypePriority MakeHigher(int DeltaAmount = 1) const
	{
		return FEditorGizmoTypePriority(Priority - DeltaAmount);
	}

	friend bool operator<(const FEditorGizmoTypePriority& l, const FEditorGizmoTypePriority& r)
	{
		return l.Priority < r.Priority;
	}
	friend bool operator==(const FEditorGizmoTypePriority& l, const FEditorGizmoTypePriority& r)
	{
		return l.Priority == r.Priority;
	}
	friend bool operator>(const FEditorGizmoTypePriority& l, const FEditorGizmoTypePriority& r)
	{
		return l.Priority > r.Priority;
	}
};

/** UInteractiveGizmoType provides actions and other information about gizmo types */
UCLASS(Transient, Abstract)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorInteractiveGizmoConditionalBuilder : public UInteractiveGizmoBuilder
{
	GENERATED_BODY()

public:

	/** Returns the priority for this gizmo type.  */
	virtual FEditorGizmoTypePriority GetPriority() const 
	{ 
		return FEditorGizmoTypePriority::DEFAULT_GIZMO_TYPE_PRIORITY;
	}

	/** Update the priority for this gizmo type.  */
	virtual void UpdatePriority(const FEditorGizmoTypePriority& InPriority)
	{
		Priority = InPriority;
	}

	/** Returns true if this gizmo is valid for creation based on the current state. */
	virtual bool SatisfiesCondition(const FToolBuilderState& SceneState) const
	{
		unimplemented();
		return false;
	}

protected:

	FEditorGizmoTypePriority Priority = FEditorGizmoTypePriority::DEFAULT_GIZMO_TYPE_PRIORITY;
};
