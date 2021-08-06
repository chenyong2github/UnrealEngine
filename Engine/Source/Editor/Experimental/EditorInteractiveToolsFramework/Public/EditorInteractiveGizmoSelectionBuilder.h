// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorInteractiveGizmoConditionalBuilder.h"
#include "ToolContextInterfaces.h"
#include "EditorInteractiveGizmoSelectionBuilder.generated.h"

class UTransformProxy;

class EDITORINTERACTIVETOOLSFRAMEWORK_API FEditorGizmoSelectionBuilderHelper
{
public:
	/**
	 * Utility method that creates a transform proxy based on the current selection.
	 */
	static UTransformProxy* CreateTransformProxyForSelection(const FToolBuilderState& SceneState);
};

UCLASS(Transient, Abstract)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorInteractiveGizmoSelectionBuilder : public UEditorInteractiveGizmoConditionalBuilder
{
	GENERATED_BODY()

public:

	/**
	 * Update gizmo's active target based on the current Editor selection and scene state.  This method creates 
	 * a transform proxy for the current selection and sets the gizmo's active target to the new transform proxy. 
	 * This method is called after a gizmo is automatically built based upon selection and also to update the 
	 * existing gizmo when the selection changes.
	 */
	virtual void UpdateGizmoForSelection(UInteractiveGizmo* Gizmo, const FToolBuilderState& SceneState) PURE_VIRTUAL(UEditorInteractiveGizmoSelectionBuilder::UpdateGizmoForSelection, return;);

};
