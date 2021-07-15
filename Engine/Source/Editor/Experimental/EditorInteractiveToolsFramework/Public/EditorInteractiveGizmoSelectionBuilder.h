// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorInteractiveGizmoConditionalBuilder.h"
#include "InteractiveGizmo.h"
#include "ToolContextInterfaces.h"
#include "EditorInteractiveGizmoSelectionBuilder.generated.h"

/** 
 * UEditorInteractiveGizmoSelectionBuilder provides a method for building and updating gizmos based on the current Editor selection 
 * and state. Builders derived from this class may be registered in one of the following places: 
 *   1) the gizmo subsystem if the gizmo should be available throughout the Editor.
 *   2) the gizmo manager if the gizmo is only used in a particular ed mode or in an asset editor.
 */
UCLASS(Transient, Abstract)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorInteractiveGizmoSelectionBuilder : public UEditorInteractiveGizmoConditionalBuilder
{
	GENERATED_BODY()

public:
	/**
	 * Build gizmo for the current Editor selection and state. The Editor gizmo manager calls this method to construct gizmos for 
	 * for the current selection. This implementation calls BuildGizmo() then UpdateGizmoForSelection(). But derived classes may provide 
	 * their own implementation which is expected to both build gizmo and set it up to manipulate the current selection. Note that 
	 * when the selection changes, the gizmo manager may reuse the gizmo and only call UpdateGizmoForSelection() on the existing gizmo.
	 */
	virtual UInteractiveGizmo* BuildGizmoForSelection(const FToolBuilderState& SceneState)
	{
		if (UInteractiveGizmo* Gizmo = BuildGizmo(SceneState))
		{
			UpdateGizmoForSelection(Gizmo, SceneState);
			return Gizmo;
		}
		return nullptr;
	}

	/**
	 * Update the input gizmo's active target based on the current Editor selection and scene state.  Derived implementations
	 * of this method should create a transform proxy for the current Editor selection and sets the gizmo's active target to the new transform proxy. 
	 * The gizmo manager calls this method when reusing a gizmo, to update the gizmo for the current selection.
	 */
	virtual void UpdateGizmoForSelection(UInteractiveGizmo* Gizmo, const FToolBuilderState& SceneState) PURE_VIRTUAL(UEditorInteractiveGizmoSelectionBuilder::UpdateGizmoForSelection, return;);

};
