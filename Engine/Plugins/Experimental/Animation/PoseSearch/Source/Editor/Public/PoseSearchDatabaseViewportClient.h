// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FPoseSearchDatabasePreviewScene;
class SPoseSearchDatabaseViewport;
class FPoseSearchDatabaseEditorToolkit;

class FPoseSearchDatabaseViewportClient : public FEditorViewportClient
{
public:

	FPoseSearchDatabaseViewportClient(
		const TSharedRef<FPoseSearchDatabasePreviewScene>& InPreviewScene, 
		const TSharedRef<SPoseSearchDatabaseViewport>& InViewport, 
		const TSharedRef<FPoseSearchDatabaseEditorToolkit>& InAssetEditorToolkit);
	virtual ~FPoseSearchDatabaseViewportClient(){}

	// ~FEditorViewportClient interface
	virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
	virtual void TrackingStarted(const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge) override;
	virtual void TrackingStopped() override;
	// ~End of FEditorViewportClient interface

	/** Get the preview scene we are viewing */
	TSharedRef<FPoseSearchDatabasePreviewScene> GetPreviewScene() const
	{
		return PreviewScenePtr.Pin().ToSharedRef(); 
	}

	TSharedRef<FPoseSearchDatabaseEditorToolkit> GetAssetEditorToolkit() const
	{
		return AssetEditorToolkitPtr.Pin().ToSharedRef(); 
	}

private:

	/** Preview scene we are viewing */
	TWeakPtr<FPoseSearchDatabasePreviewScene> PreviewScenePtr;

	/** Asset editor toolkit we are embedded in */
	TWeakPtr<FPoseSearchDatabaseEditorToolkit> AssetEditorToolkitPtr;
};
