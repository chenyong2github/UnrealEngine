// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FCanvas;
class FDisplayClusterConfiguratorPreviewScene;
class SDisplayClusterConfiguratorViewport;
class FDisplayClusterConfiguratorToolkit;


class FDisplayClusterConfiguratorEditorViewportClient
	: public FEditorViewportClient
	, public TSharedFromThis<FDisplayClusterConfiguratorEditorViewportClient>
{
public:
	FDisplayClusterConfiguratorEditorViewportClient(const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene, const TSharedRef<SDisplayClusterConfiguratorViewport>& InEditorViewport, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit);

	void Build();

public:
	//~ Begin FEditorViewportClient interface
	virtual void Tick(float DeltaSeconds) override;
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void DrawCanvas(FViewport& InViewport, FSceneView& View, FCanvas& Canvas) override;
	virtual void ProcessClick(class FSceneView& View, class HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
	//~ End FEditorViewportClient interface

private:
	/** Invalidate this view in response to a preview scene change */
	void HandleInvalidateViews();

	TWeakPtr<FDisplayClusterConfiguratorPreviewScene> PreviewScenePtr;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;
};
