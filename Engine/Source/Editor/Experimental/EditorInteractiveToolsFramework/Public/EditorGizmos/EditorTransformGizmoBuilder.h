// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BaseGizmos/TransformProxy.h"
#include "EditorInteractiveGizmoSelectionBuilder.h"
#include "EditorTransformGizmoBuilder.generated.h"

class UInteractiveGizmo;

UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorTransformGizmoBuilder : public UInteractiveGizmoBuilder, public IEditorInteractiveGizmoSelectionBuilder
{
	GENERATED_BODY()

public:

	// UEditorInteractiveGizmoSelectionBuilder interface 
	virtual UInteractiveGizmo* BuildGizmo(const FToolBuilderState& SceneState) const override;
	virtual void UpdateGizmoForSelection(UInteractiveGizmo* Gizmo, const FToolBuilderState& SceneState) override;
};


