// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorUndoClient.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "SEditorViewport.h"

class FDisplayClusterConfiguratorToolkit;
class SDisplayClusterConfiguratorViewViewport;
class FDisplayClusterConfiguratorPreviewScene;
class FDisplayClusterConfiguratorEditorViewportClient;

class FEditorViewportClient;
class SOverlay;


class SDisplayClusterConfiguratorViewport
	: public FEditorUndoClient
	, public SEditorViewport
{

public:
	SLATE_BEGIN_ARGS(SDisplayClusterConfiguratorViewport)
	{}

	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit, const TSharedRef<FDisplayClusterConfiguratorPreviewScene>& InPreviewScene);

protected:
	//~ Begin SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
	virtual void OnFocusViewportToSelection() override;
	virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
	//~ End of SEditorViewport interface

	//~ Begin FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess);
	virtual void PostRedo(bool bSuccess);
	//~ End of FEditorUndoClient interface

private:
	// Pointer to the compound widget that owns this viewport widget
	TWeakPtr<SDisplayClusterConfiguratorViewViewport> ViewPtr;

	TWeakPtr<FDisplayClusterConfiguratorToolkit> ToolkitPtr;

	TWeakPtr<FDisplayClusterConfiguratorPreviewScene> PreviewScenePtr;

	TSharedPtr<FDisplayClusterConfiguratorEditorViewportClient> ViewportClient;
};
