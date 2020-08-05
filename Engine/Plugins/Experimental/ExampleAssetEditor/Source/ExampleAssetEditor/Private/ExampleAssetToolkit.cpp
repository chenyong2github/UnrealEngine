// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExampleAssetToolkit.h"

#include "AssetEditorModeManager.h"
#include "GizmoEdMode.h"
#include "Tools/UAssetEditor.h"
#include "EditorViewportClientWrapper.h"
#include "PreviewScene.h"
#include "InteractiveToolsContext.h"
#include "ExampleToolsContextInterfaces.h"
#include "ExampleAssetEditorViewport.h"
#include "Viewports.h"
#include "GizmoEdMode.h"
#include "EditorModeManager.h"

FExampleAssetToolkit::FExampleAssetToolkit(UAssetEditor* InOwningAssetEditor, UInteractiveToolsContext* InContext)
    : FBaseAssetToolkit(InOwningAssetEditor)
	, ToolsContext(InContext)
{
	check(ToolsContext);

	ToolsContextQueries = MakeShareable(new FToolsContextQueriesImpl(ToolsContext));
	ToolsContextTransactions = MakeShareable(new FToolsContextTransactionImpl);
	ToolsContext->Initialize(ToolsContextQueries.Get(), ToolsContextTransactions.Get());
}

FExampleAssetToolkit::~FExampleAssetToolkit()
{
}

AssetEditorViewportFactoryFunction FExampleAssetToolkit::GetViewportDelegate()
{
	AssetEditorViewportFactoryFunction TempViewportDelegate = [this](const FAssetEditorViewportConstructionArgs InArgs)
	{
		return SNew(SExampleAssetEditorViewport, InArgs)
			.EditorViewportClient(ViewportClient)
			.InputRouter(ToolsContext->InputRouter);
	};

	return TempViewportDelegate;
}

TSharedPtr<FEditorViewportClient> FExampleAssetToolkit::CreateEditorViewportClient() const
{
	// Leaving the preview scene to nullptr default creates us a viewport that mirrors the main level editor viewport
	TSharedPtr<FEditorViewportClient> WrappedViewportClient = MakeShared<FEditorViewportClientWrapper>(ToolsContext, EditorModeManager.Get(), nullptr);
	WrappedViewportClient->SetViewLocation(EditorViewportDefs::DefaultPerspectiveViewLocation);
	WrappedViewportClient->SetViewRotation(EditorViewportDefs::DefaultPerspectiveViewRotation);
	return WrappedViewportClient;
}

void FExampleAssetToolkit::PostInitAssetEditor()
{
	GetEditorModeManager().ActivateMode(GetDefault<UGizmoEdMode>()->GetID());
}
