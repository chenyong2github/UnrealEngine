// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FContextualAnimPreviewScene;
class SContextualAnimViewport;
class FContextualAnimAssetEditorToolkit;

class FContextualAnimViewportClient : public FEditorViewportClient
{
public:

	FContextualAnimViewportClient(const TSharedRef<FContextualAnimPreviewScene>& InPreviewScene, const TSharedRef<SContextualAnimViewport>& InViewport, const TSharedRef<FContextualAnimAssetEditorToolkit>& InAssetEditorToolkit);
	virtual ~FContextualAnimViewportClient(){}

	// ~FEditorViewportClient interface
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void TrackingStarted(const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge) override;
	virtual void TrackingStopped() override;
	// ~End of FEditorViewportClient interface

	/** Get the preview scene we are viewing */
	TSharedRef<FContextualAnimPreviewScene> GetPreviewScene() const { return PreviewScenePtr.Pin().ToSharedRef(); }

	TSharedRef<FContextualAnimAssetEditorToolkit> GetAssetEditorToolkit() const { return AssetEditorToolkitPtr.Pin().ToSharedRef(); }

private:

	/** Preview scene we are viewing */
	TWeakPtr<FContextualAnimPreviewScene> PreviewScenePtr;

	/** Asset editor toolkit we are embedded in */
	TWeakPtr<FContextualAnimAssetEditorToolkit> AssetEditorToolkitPtr;
};
