// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"

class FContextualAnimPreviewScene;
class FContextualAnimViewportClient;
class FContextualAnimAssetEditorToolkit;

struct FContextualAnimViewportRequiredArgs
{
	FContextualAnimViewportRequiredArgs(const TSharedRef<FContextualAnimAssetEditorToolkit>& InAssetEditorToolkit, const TSharedRef<FContextualAnimPreviewScene>& InPreviewScene)
		: AssetEditorToolkit(InAssetEditorToolkit)
		, PreviewScene(InPreviewScene)
	{}

	TSharedRef<FContextualAnimAssetEditorToolkit> AssetEditorToolkit;

	TSharedRef<FContextualAnimPreviewScene> PreviewScene;
};

class SContextualAnimViewport : public SEditorViewport
{
public:
	
	SLATE_BEGIN_ARGS(SContextualAnimViewport) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, const FContextualAnimViewportRequiredArgs& InRequiredArgs);
	virtual ~SContextualAnimViewport(){}

protected:

	// ~SEditorViewport interface
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	// ~End of SEditorViewport interface

protected:
	
	/** Viewport client */
	TSharedPtr<FContextualAnimViewportClient> ViewportClient;

	/** The preview scene that we are viewing */
	TWeakPtr<FContextualAnimPreviewScene> PreviewScenePtr;

	/** Asset editor toolkit we are embedded in */
	TWeakPtr<FContextualAnimAssetEditorToolkit> AssetEditorToolkitPtr;
};