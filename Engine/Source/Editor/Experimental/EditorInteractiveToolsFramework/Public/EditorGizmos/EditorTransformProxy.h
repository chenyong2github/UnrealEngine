// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "BaseGizmos/TransformProxy.h"
#include "EditorTransformProxy.generated.h"

/**
 * UEditorTransformProxy is a derivation of UTransformProxy that
 * returns the transform that defines the current space of the default
 * Editor transform gizmo for a given mode manager / viewport.
 * 
 * @todo Currently this defaults internally to GLevelEditorModeManager()
 * but eventually it should be possible to set and use a different mode
 * manager.
 */
UCLASS(Transient)
class EDITORINTERACTIVETOOLSFRAMEWORK_API UEditorTransformProxy : public UTransformProxy
{
	GENERATED_BODY()
public:

	/**
	 * @return the shared transform for all the sub-objects
	 */
	virtual FTransform GetTransform() const override;

	/**
	 * Update the main transform and then update the sub-objects based on their relative transformations
	 */
	virtual void SetTransform(const FTransform& Transform) override;

};

