// Copyright Epic Games, Inc.All Rights Reserved.

#include "LevelAssetEditorViewport.h"

#include "InputRouter.h"
#include "LevelEditorViewportInterface.h"
#include "Slate/SceneViewport.h"
#include "LevelEditorViewport.h"

void SLevelAssetEditorViewport::Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs)
{
	InputRouter = InArgs._InputRouter;

	// Construct the slate editor viewport
	SAssetEditorViewport::Construct(
		SAssetEditorViewport::FArguments()
			.EditorViewportClient(InArgs._EditorViewportClient),
		InViewportConstructionArgs);

	// Override the viewport interface with our input router wrapper
	SlateInputWrapper = MakeShared<FLevelEditorViewportInterfaceWrapper>(SceneViewport, InputRouter);
	ViewportWidget->SetViewportInterface(SlateInputWrapper.ToSharedRef());
}
