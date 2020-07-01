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

TFunction<TSharedRef<SEditorViewport>(void)> FExampleAssetToolkit::GetViewportDelegate()
{
	TFunction<TSharedRef<SEditorViewport>(void)> TempViewportDelegate = [=]()
	{
		return SNew(SExampleAssetEditorViewport)
			.EditorViewportClient(ViewportClient)
			.InputRouter(ToolsContext->InputRouter);
	};

	return TempViewportDelegate;
}

TSharedPtr<FEditorViewportClient> FExampleAssetToolkit::CreateEditorViewportClient() const
{
	FPreviewScene* PreviewScene = new FPreviewScene(FPreviewScene::ConstructionValues());
	StaticCastSharedPtr<FAssetEditorModeManager>(EditorModeManager)->SetPreviewScene(PreviewScene);

	return MakeShared<FEditorViewportClientWrapper>(ToolsContext, EditorModeManager.Get(), PreviewScene);
}

void FExampleAssetToolkit::CreateEditorModeManager()
{
	FBaseAssetToolkit::CreateEditorModeManager();

	EditorModeManager->ActivateMode(GetDefault<UGizmoEdMode>()->GetID());
}
