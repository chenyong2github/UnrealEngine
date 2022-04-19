// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchDatabaseEdMode.h"
#include "PoseSearchDatabaseViewportClient.h"
#include "PoseSearchDatabaseEditorToolkit.h"
#include "PoseSearchDatabaseViewModel.h"
#include "PoseSearch/PoseSearch.h"

#include "EngineUtils.h"

const FEditorModeID FPoseSearchDatabaseEdMode::EdModeId = TEXT("PoseSearchDatabaseEdMode");

FPoseSearchDatabaseEdMode::FPoseSearchDatabaseEdMode()
{
}

FPoseSearchDatabaseEdMode::~FPoseSearchDatabaseEdMode()
{
}

void FPoseSearchDatabaseEdMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	FPoseSearchDatabaseViewportClient* PoseSearchDbViewportClient = 
		static_cast<FPoseSearchDatabaseViewportClient*>(ViewportClient);
	if (!ViewModel && PoseSearchDbViewportClient)
	{
		ViewModel = PoseSearchDbViewportClient->GetAssetEditorToolkit()->GetViewModel();
	}

	if (ViewModel)
	{
		ViewModel->Tick(DeltaTime);
	}
}

void FPoseSearchDatabaseEdMode::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	FEdMode::Render(View, Viewport, PDI);
}

bool FPoseSearchDatabaseEdMode::HandleClick(
	FEditorViewportClient* InViewportClient, 
	HHitProxy* HitProxy, 
	const FViewportClick& Click)
{
	return FEdMode::HandleClick(InViewportClient, HitProxy, Click);
}


bool FPoseSearchDatabaseEdMode::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return FEdMode::StartTracking(InViewportClient, InViewport);
}

bool FPoseSearchDatabaseEdMode::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	return FEdMode::EndTracking(InViewportClient, InViewport);
}

bool FPoseSearchDatabaseEdMode::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	return FEdMode::InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale);
}

bool FPoseSearchDatabaseEdMode::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	return FEdMode::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FPoseSearchDatabaseEdMode::AllowWidgetMove()
{
	return FEdMode::ShouldDrawWidget();
}

bool FPoseSearchDatabaseEdMode::ShouldDrawWidget() const
{
	return FEdMode::ShouldDrawWidget();
}

bool FPoseSearchDatabaseEdMode::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return FEdMode::GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

bool FPoseSearchDatabaseEdMode::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return FEdMode::GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

FVector FPoseSearchDatabaseEdMode::GetWidgetLocation() const
{
	return FEdMode::GetWidgetLocation();
}
