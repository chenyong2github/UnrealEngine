// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetEditorGizmoFactory.h"

#include "DefaultAssetEditorGizmoFactory.generated.h"

UCLASS()
class GIZMOEDMODE_API UDefaultAssetEditorGizmoFactory : public UObject, public IAssetEditorGizmoFactory
{
	GENERATED_BODY()
public:
	//IAssetEditorGizmoFactory interface
	virtual bool CanBuildGizmoForSelection(FEditorModeTools* ModeTools) const override;
	virtual UTransformGizmo* BuildGizmoForSelection(FEditorModeTools* ModeTools, UInteractiveGizmoManager* GizmoManager) const override;
	virtual EAssetEditorGizmoFactoryPriority GetPriority() const override { return EAssetEditorGizmoFactoryPriority::Default; }
	virtual void ConfigureGridSnapping(bool bGridEnabled, bool bRotGridEnabled, UTransformGizmo* Gizmo) const override;
};
