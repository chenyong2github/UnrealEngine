// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelAssetEditorToolkit.h"

#include "AssetEditorModeManager.h"
#include "Tools/UAssetEditor.h"
#include "LevelAssetEditorViewportClient.h"
#include "InteractiveToolsContext.h"
#include "LevelEditorToolsContextInterfaces.h"
#include "LevelAssetEditorViewport.h"
#include "Viewports.h"
#include "EditorModeManager.h"

FLevelEditorAssetToolkit::FLevelEditorAssetToolkit(UAssetEditor* InOwningAssetEditor, UInteractiveToolsContext* InContext)
    : FBaseAssetToolkit(InOwningAssetEditor)
	, ToolsContext(InContext)
{
	check(ToolsContext);

	ToolsContextQueries = MakeShareable(new FLevelEditorToolsContextQueriesImpl(ToolsContext));
	ToolsContextTransactions = MakeShareable(new FLevelEditorContextTransactionImpl);
	ToolsContext->Initialize(ToolsContextQueries.Get(), ToolsContextTransactions.Get());
}

FLevelEditorAssetToolkit::~FLevelEditorAssetToolkit()
{
}

AssetEditorViewportFactoryFunction FLevelEditorAssetToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction TempViewportDelegate = [this](const FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SLevelAssetEditorViewport, InArgs)
			.EditorViewportClient(ViewportClient)
			.InputRouter(ToolsContext->InputRouter);
	};

	return TempViewportDelegate;
}

TSharedPtr<FEditorViewportClient> FLevelEditorAssetToolkit::CreateEditorViewportClient() const
{
	// Leaving the preview scene to nullptr default creates us a viewport that mirrors the main level editor viewport
	TSharedPtr<FEditorViewportClient> WrappedViewportClient = MakeShared<FLevelAssetEditorViewportClient>(ToolsContext, EditorModeManager.Get(), nullptr);
	WrappedViewportClient->SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	WrappedViewportClient->SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);
	return WrappedViewportClient;
}

void FLevelEditorAssetToolkit::PostInitAssetEditor()
{
}
