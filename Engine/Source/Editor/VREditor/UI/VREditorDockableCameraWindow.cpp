// Copyright Epic Games, Inc. All Rights Reserved.

#include "VREditorDockableCameraWindow.h"
#include "VREditorCameraWidgetComponent.h"

AVREditorDockableCameraWindow::AVREditorDockableCameraWindow(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer.SetDefaultSubobjectClass<UVREditorCameraWidgetComponent>(TEXT("WidgetComponent")))
{
}

