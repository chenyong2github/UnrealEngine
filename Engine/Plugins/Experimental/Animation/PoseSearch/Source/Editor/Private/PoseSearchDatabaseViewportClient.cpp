// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseViewportClient.h"
#include "PoseSearchDatabaseEditorToolkit.h"
#include "PoseSearchDatabasePreviewScene.h"
#include "SPoseSearchDatabaseViewport.h"
#include "AssetEditorModeManager.h"
#include "UnrealWidget.h"
#include "PoseSearchDatabaseEdMode.h"

FPoseSearchDatabaseViewportClient::FPoseSearchDatabaseViewportClient(
	const TSharedRef<FPoseSearchDatabasePreviewScene>& InPreviewScene,
	const TSharedRef<SPoseSearchDatabaseViewport>& InViewport, 
	const TSharedRef<FPoseSearchDatabaseEditorToolkit>& InAssetEditorToolkit)
	: FEditorViewportClient(nullptr, &InPreviewScene.Get(), StaticCastSharedRef<SEditorViewport>(InViewport))
	, PreviewScenePtr(InPreviewScene)
	, AssetEditorToolkitPtr(InAssetEditorToolkit)
{
	Widget->SetUsesEditorModeTools(ModeTools.Get());
	StaticCastSharedPtr<FAssetEditorModeManager>(ModeTools)->SetPreviewScene(&InPreviewScene.Get());
	ModeTools->SetDefaultMode(FPoseSearchDatabaseEdMode::EdModeId);

	SetRealtime(true);

	SetWidgetCoordSystemSpace(COORD_Local);
	ModeTools->SetWidgetMode(UE::Widget::WM_Translate);
}

void FPoseSearchDatabaseViewportClient::TrackingStarted(
	const struct FInputEventState& InInputState, 
	bool bIsDraggingWidget, 
	bool bNudge)
{
	ModeTools->StartTracking(this, Viewport);
}

void FPoseSearchDatabaseViewportClient::TrackingStopped()
{
	ModeTools->EndTracking(this, Viewport);
	Invalidate();
}

void FPoseSearchDatabaseViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
}
