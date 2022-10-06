// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SAssetEditorViewport.h"

/**
 * Viewport used for 3D preview in cloth editor. Has a custom toolbar overlay at the top.
 */
class CHAOSCLOTHASSETEDITOR_API SChaosClothAssetEditor3DViewport : public SAssetEditorViewport 
{

public:
	// SAssetEditorViewport
	virtual void BindCommands() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;

};
