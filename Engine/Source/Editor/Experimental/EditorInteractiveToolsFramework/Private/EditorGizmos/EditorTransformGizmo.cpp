// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/EditorTransformGizmo.h"
#include "EditorGizmos/EditorTransformProxy.h"

#define LOCTEXT_NAMESPACE "UEditorTransformGizmo"

DEFINE_LOG_CATEGORY_STATIC(LogEditorTransformGizmo, Log, All);

void UEditorTransformGizmo::ApplyTranslateDelta(const FVector& InTranslateDelta)
{
	check(ActiveTarget);

	if (UEditorTransformProxy* EditorTransformProxy = Cast<UEditorTransformProxy>(ActiveTarget))
	{
		EditorTransformProxy->InputTranslateDelta(InTranslateDelta, InteractionAxisType);

		// Update the cached current transform
		CurrentTransform.AddToTranslation(InTranslateDelta);
	}
	else
	{
		Super::ApplyTranslateDelta(InTranslateDelta);
	}
}

void UEditorTransformGizmo::ApplyScaleDelta(const FVector& InScaleDelta)
{
	check(ActiveTarget);

	if (UEditorTransformProxy* EditorTransformProxy = Cast<UEditorTransformProxy>(ActiveTarget))
	{
		FVector StartScale = CurrentTransform.GetScale3D();

		EditorTransformProxy->InputScaleDelta(InScaleDelta, InteractionAxisType);

		// Update the cached current transform
		FVector NewScale = StartScale + InScaleDelta * InteractionAxis;
		CurrentTransform.SetScale3D(NewScale);
	}
	else
	{
		Super::ApplyScaleDelta(InScaleDelta);
	}
}

#undef LOCTEXT_NAMESPACE
