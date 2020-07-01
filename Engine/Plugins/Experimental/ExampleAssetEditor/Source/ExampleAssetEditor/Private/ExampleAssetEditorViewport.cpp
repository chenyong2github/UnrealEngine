// Copyright Epic Games, Inc.All Rights Reserved.

#include "ExampleAssetEditorViewport.h"

#include "InputRouter.h"
#include "SlateViewportInterfaceWrapper.h"
#include "Slate/SceneViewport.h"

void SExampleAssetEditorViewport::Construct(const FArguments& InArgs)
{
	InputRouter = InArgs._InputRouter;

	// Construct the slate editor viewport
	SAssetEditorViewport::Construct(SAssetEditorViewport::FArguments()
		.EditorViewportClient(InArgs._EditorViewportClient));

	// Override the viewport interface with our input router wrapper
	SlateInputWrapper = MakeShared<FSlateViewportInterfaceWrapper>(SceneViewport, InputRouter);
	ViewportWidget->SetViewportInterface(SlateInputWrapper.ToSharedRef());
}
