// Copyright Epic Games, Inc.All Rights Reserved.

#include "EditorViewportClientWrapper.h"
#include "UnrealWidget.h"

FEditorViewportClientWrapper::FEditorViewportClientWrapper(UInteractiveToolsContext* InToolsContext, FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget)
	, ToolsContext(InToolsContext)
{
	Widget->SetUsesEditorModeTools(ModeTools.Get());
}
