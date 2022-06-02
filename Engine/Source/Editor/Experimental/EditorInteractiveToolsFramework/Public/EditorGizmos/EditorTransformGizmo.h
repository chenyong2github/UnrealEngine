// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorGizmos/TransformGizmo.h"
#include "EditorTransformGizmo.generated.h"

/**
 * UEditorTransformGizmo handles Editor-specific functionality for the TransformGizmo,
 * applied to a UEditorTransformProxy target object.
 */
UCLASS()
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorTransformGizmo : public UTransformGizmo
{
	GENERATED_BODY()

protected:

	/** Update current gizmo mode based on transform source */
	virtual void Translate(const FVector& InTranslateDelta) override;
};