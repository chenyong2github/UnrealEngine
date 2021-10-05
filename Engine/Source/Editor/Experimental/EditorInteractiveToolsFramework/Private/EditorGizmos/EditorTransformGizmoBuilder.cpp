// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/EditorTransformGizmoBuilder.h"
#include "EditorGizmos/EditorTransformGizmoSource.h"
#include "EditorGizmos/EditorTransformProxy.h"
#include "EditorGizmos/TransformGizmo.h"

UInteractiveGizmo* UEditorTransformGizmoBuilder::BuildGizmo(const FToolBuilderState& SceneState) const
{
	UTransformGizmo* TransformGizmo = NewObject<UTransformGizmo>(SceneState.GizmoManager);
	TransformGizmo->Setup();
	TransformGizmo->TransformSource = UEditorTransformGizmoSource::Construct(TransformGizmo);
	return TransformGizmo;
}

void UEditorTransformGizmoBuilder::UpdateGizmoForSelection(UInteractiveGizmo* Gizmo, const FToolBuilderState& SceneState)
{
	if (UTransformGizmo* TransformGizmo = Cast<UTransformGizmo>(Gizmo))
	{
		UEditorTransformProxy* TransformProxy = NewObject<UEditorTransformProxy>();
		TransformGizmo->SetActiveTarget(TransformProxy);
		TransformGizmo->SetVisibility(true);
	}
}
