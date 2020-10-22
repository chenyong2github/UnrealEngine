// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ControlRigEditMode.h"
#include "IPersonaEditMode.h"
#include "Preferences/PersonaOptions.h"
#include "Persona/Private/AnimationEditorViewportClient.h"

#pragma once

class FControlRigEditorEditMode : public FControlRigEditMode
{
public:
	static FName ModeName;

	/** FControlRigEditMode interface */
	virtual bool IsInLevelEditor() const { return false; }

	// FEdMode interface
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;

	/* IPersonaEditMode interface */
	virtual bool GetCameraTarget(FSphere& OutTarget) const override;
	virtual class IPersonaPreviewScene& GetAnimPreviewScene() const override;
	virtual void GetOnScreenDebugInfo(TArray<FText>& OutDebugInfo) const override;

	/** If set to true the edit mode will additionally render all bones */
	bool bDrawHierarchyBones = false;

	/** Drawing options */
	UPersonaOptions* ConfigOption = nullptr;

	/** Flag to know if a bone is visible or not */
	TArray<bool> BoneHasSelectedChild;
};