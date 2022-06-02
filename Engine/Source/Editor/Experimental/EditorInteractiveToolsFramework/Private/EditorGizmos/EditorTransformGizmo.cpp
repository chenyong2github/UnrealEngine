// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/EditorTransformGizmo.h"
#include "EditorGizmos/EditorTransformProxy.h"

#define LOCTEXT_NAMESPACE "UEditorTransformGizmo"

DEFINE_LOG_CATEGORY_STATIC(LogEditorTransformGizmo, Log, All);

void UEditorTransformGizmo::Translate(const FVector& InTranslateDelta)
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
		Super::Translate(InTranslateDelta);
	}
}

#undef LOCTEXT_NAMESPACE
