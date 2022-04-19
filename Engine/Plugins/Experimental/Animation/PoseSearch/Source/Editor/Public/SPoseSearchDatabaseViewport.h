// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class FPoseSearchDatabasePreviewScene;
class FPoseSearchDatabaseViewportClient;
class FPoseSearchDatabaseEditorToolkit;
class SPoseSearchDatabaseViewportToolBar;

struct FPoseSearchDatabaseViewportRequiredArgs
{
	FPoseSearchDatabaseViewportRequiredArgs(
		const TSharedRef<FPoseSearchDatabaseEditorToolkit>& InAssetEditorToolkit, 
		const TSharedRef<FPoseSearchDatabasePreviewScene>& InPreviewScene)
		: AssetEditorToolkit(InAssetEditorToolkit)
		, PreviewScene(InPreviewScene)
	{}

	TSharedRef<FPoseSearchDatabaseEditorToolkit> AssetEditorToolkit;

	TSharedRef<FPoseSearchDatabasePreviewScene> PreviewScene;
};

class SPoseSearchDatabaseViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
public:
	
	SLATE_BEGIN_ARGS(SPoseSearchDatabaseViewport) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, const FPoseSearchDatabaseViewportRequiredArgs& InRequiredArgs);
	virtual ~SPoseSearchDatabaseViewport(){}

	// ~ICommonEditorViewportToolbarInfoProvider interface
	virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;
	// ~End of ICommonEditorViewportToolbarInfoProvider interface

protected:

	// ~SEditorViewport interface
	virtual void BindCommands() override;
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
	// ~End of SEditorViewport interface

	/** The viewport toolbar */
	TSharedPtr<SPoseSearchDatabaseViewportToolBar> ViewportToolbar;

	/** Viewport client */
	TSharedPtr<FPoseSearchDatabaseViewportClient> ViewportClient;

	/** The preview scene that we are viewing */
	TWeakPtr<FPoseSearchDatabasePreviewScene> PreviewScenePtr;

	/** Asset editor toolkit we are embedded in */
	TWeakPtr<FPoseSearchDatabaseEditorToolkit> AssetEditorToolkitPtr;
};