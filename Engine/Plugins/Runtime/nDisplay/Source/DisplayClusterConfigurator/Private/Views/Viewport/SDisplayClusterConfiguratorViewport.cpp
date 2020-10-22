// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/SDisplayClusterConfiguratorViewport.h"

#include "DisplayClusterConfiguratorToolkit.h"
#include "Views/Viewport/DisplayClusterConfiguratorEditorViewportClient.h"

#define LOCTEXT_NAMESPACE "SDisplayClusterConfiguratorViewport"


void SDisplayClusterConfiguratorViewport::Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene)
{
	PreviewScenePtr = InPreviewScene;
	ToolkitPtr = InToolkit;

	SEditorViewport::Construct(
		SEditorViewport::FArguments()
			.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
			.AddMetaData<FTagMetaData>(TEXT("DisplayClusterConfigurator.Viewport"))
		);
}

TSharedRef<FEditorViewportClient> SDisplayClusterConfiguratorViewport::MakeEditorViewportClient()
{
	// Create an animation viewport client
	ViewportClient = MakeShared<FDisplayClusterConfiguratorEditorViewportClient>(PreviewScenePtr.Pin().ToSharedRef(), SharedThis(this), ToolkitPtr.Pin().ToSharedRef());
	ViewportClient->Build();
	ViewportClient->SetRealtime(true);

	ViewportClient->ViewportType = LVT_Perspective;
	ViewportClient->bSetListenerPosition = false;

	ViewportClient->SetViewLocation(FVector(-1024.0f, 0.f, 512.0f));
	ViewportClient->SetViewRotation(FRotator(-15.0f, 0.0f, 0));

	return ViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDisplayClusterConfiguratorViewport::MakeViewportToolbar()
{
	return SEditorViewport::MakeViewportToolbar();
}

void SDisplayClusterConfiguratorViewport::OnFocusViewportToSelection()
{
}

void SDisplayClusterConfiguratorViewport::PopulateViewportOverlays(TSharedRef<SOverlay> Overlay)
{
	SEditorViewport::PopulateViewportOverlays(Overlay);
}

void SDisplayClusterConfiguratorViewport::PostUndo(bool bSuccess)
{
}

void SDisplayClusterConfiguratorViewport::PostRedo(bool bSuccess)
{
}

#undef LOCTEXT_NAMESPACE
