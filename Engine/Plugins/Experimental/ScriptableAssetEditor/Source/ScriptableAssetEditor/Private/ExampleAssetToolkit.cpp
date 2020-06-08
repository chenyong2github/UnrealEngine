// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExampleAssetToolkit.h"

#include "AssetEditorModeManager.h"
#include "GizmoEdMode.h"
#include "Subsystems/AssetEditorSubsystem.h"
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

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	WindowOpenedDelegateHandle = AssetEditorSubsystem->OnAssetOpenedInEditor().AddRaw(this, &FExampleAssetToolkit::OnAssetOpened);

	ToolsContextQueries = MakeShareable(new FToolsContextQueriesImpl(ToolsContext));
	ToolsContextTransactions = MakeShareable(new FToolsContextTransactionImpl);
	ToolsContext->Initialize(ToolsContextQueries.Get(), ToolsContextTransactions.Get());
}

FExampleAssetToolkit::~FExampleAssetToolkit()
{
	ViewportClient->GetModeTools()->DeactivateMode(GetDefault<UGizmoEdMode>()->GetID());
	if (WindowOpenedDelegateHandle.IsValid())
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(WindowOpenedDelegateHandle);
	}
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
	return MakeShared<FEditorViewportClientWrapper>(ToolsContext, nullptr, PreviewScene);
}

void FExampleAssetToolkit::OnAssetOpened(UObject* Asset, IAssetEditorInstance* AssetEditorInstance)
{	
	if (AssetEditorInstance == static_cast<IAssetEditorInstance*>(this))
	{
		FAssetEditorModeManager* ModeManager = static_cast<FAssetEditorModeManager*>(ViewportClient->GetModeTools());
		ModeManager->SetToolkitHost(GetToolkitHost());
		ModeManager->ActivateMode(GetDefault<UGizmoEdMode>()->GetID());
		ModeManager->SetPreviewScene(ViewportClient->GetPreviewScene());
		
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		AssetEditorSubsystem->OnAssetOpenedInEditor().Remove(WindowOpenedDelegateHandle);
		WindowOpenedDelegateHandle.Reset();
	}
}
