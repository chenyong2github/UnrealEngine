// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VREditorDockableWindow.h"
#include "VREditorDockableCameraWindow.generated.h"

/**
 * A specialized dockable window for camera viewfinders in VR that applies the correct material
 */
UCLASS()
class AVREditorDockableCameraWindow : public AVREditorDockableWindow
{
	GENERATED_BODY()
	
public:

	/** Default constructor */
  AVREditorDockableCameraWindow(const FObjectInitializer& ObjectInitializer);
};

