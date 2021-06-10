// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorInteractiveGizmoConditionalBuilder.h"
#include "ToolContextInterfaces.h"
#include "EditorInteractiveGizmoSelectionBuilder.generated.h"


/** 
 * UEditorInteractiveGizmoSelectionBuilder provides a method for checking that the current selection and widget mode satisfy 
 * the conditions of this builder. Builders derived from this class should be registered in the gizmo subsystem, for gizmos
 * available globally in the Editor, or in the gizmo manager for gizmos only relevant to a particle ed mode or asset editor.
 */
UCLASS(Transient, Abstract)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorInteractiveGizmoSelectionBuilder : public UEditorInteractiveGizmoConditionalBuilder
{
	GENERATED_BODY()

public:

	/** Returns true if this gizmo is valid for creation based on the current state. */
	virtual bool SatisfiesCondition(const FToolBuilderState& SceneState) const override
	{
		return false;
	}
};
