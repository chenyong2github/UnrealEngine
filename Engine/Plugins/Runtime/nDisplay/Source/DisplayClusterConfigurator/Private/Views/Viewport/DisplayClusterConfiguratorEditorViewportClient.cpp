// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/Viewport/DisplayClusterConfiguratorEditorViewportClient.h"
#include "Views/Viewport/SDisplayClusterConfiguratorViewport.h"
#include "DisplayClusterConfiguratorEditorViewportClient.h"
#include "DisplayClusterConfiguratorToolkit.h"
#include "DisplayClusterConfiguratorPreviewScene.h"
#include "Views/Viewport/DisplayClusterConfiguratorActor.h"

#include "EngineUtils.h"


FDisplayClusterConfiguratorEditorViewportClient::FDisplayClusterConfiguratorEditorViewportClient(const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene, const TSharedRef<SDisplayClusterConfiguratorViewport>& InEditorViewport, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: FEditorViewportClient(nullptr, &InPreviewScene.Get(), StaticCastSharedRef<SEditorViewport>(InEditorViewport))
{
	EngineShowFlags.SetSelectionOutline(true);
	PreviewScenePtr = InPreviewScene;
	ToolkitPtr = InToolkit;
}

void FDisplayClusterConfiguratorEditorViewportClient::Build()
{
	// Register delegates
	ToolkitPtr.Pin()->RegisterOnInvalidateViews(IDisplayClusterConfiguratorToolkit::FOnInvalidateViewsDelegate::CreateSP(this, &FDisplayClusterConfiguratorEditorViewportClient::HandleInvalidateViews));
}

void FDisplayClusterConfiguratorEditorViewportClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
}

void FDisplayClusterConfiguratorEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);
}

void FDisplayClusterConfiguratorEditorViewportClient::DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
}

void FDisplayClusterConfiguratorEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	ToolkitPtr.Pin()->ClearViewportSelection();

	if (HitProxy)
	{
		if (HitProxy->IsA(HActor::StaticGetType()))
		{
			HActor* ActorProxy = (HActor*)HitProxy;
			if (ActorProxy && ActorProxy->Actor && ActorProxy->PrimComponent)
			{
				if (ADisplayClusterConfiguratorActor* ConfiguratorActor = Cast<ADisplayClusterConfiguratorActor>(ActorProxy->Actor))
				{
					ConfiguratorActor->SetNodeSelection(true);
				}
			}

			Invalidate();
			return;
		}
	}
	
	FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
}

void FDisplayClusterConfiguratorEditorViewportClient::HandleInvalidateViews()
{
	Invalidate();
}
