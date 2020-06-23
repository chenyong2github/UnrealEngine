// Copyright Epic Games, Inc.All Rights Reserved.

#include "EditorViewportClientWrapper.h"

FEditorViewportClientWrapper::FEditorViewportClientWrapper(UInteractiveToolsContext* InToolsContext, FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
	: FEditorViewportClient(InModeTools, InPreviewScene, InEditorViewportWidget)
	, ToolsContext(InToolsContext)
{
}
