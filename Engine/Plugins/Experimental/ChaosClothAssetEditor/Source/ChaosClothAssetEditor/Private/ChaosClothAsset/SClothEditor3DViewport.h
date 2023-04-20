// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SAssetEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SBaseCharacterFXEditorViewport.h"

class FChaosClothPreviewScene;
class SClothAnimationScrubPanel;

/**
 * Viewport used for 3D preview in cloth editor. Has a custom toolbar overlay at the top.
 */
class CHAOSCLOTHASSETEDITOR_API SChaosClothAssetEditor3DViewport : public SBaseCharacterFXEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{

public:

	SLATE_BEGIN_ARGS(SChaosClothAssetEditor3DViewport) {}
		SLATE_ATTRIBUTE(FVector2D, ViewportSize);
		SLATE_ARGUMENT(TSharedPtr<FEditorViewportClient>, EditorViewportClient)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const FAssetEditorViewportConstructionArgs& InViewportConstructionArgs);

	// SAssetEditorViewport
	virtual void BindCommands() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;

	virtual void OnFocusViewportToSelection() override;

	// ICommonEditorViewportToolbarInfoProvider
	virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override {}

private:

	TWeakPtr<FChaosClothPreviewScene> GetPreviewScene();
	TWeakPtr<const FChaosClothPreviewScene> GetPreviewScene() const;

	float GetViewMinInput() const;
	float GetViewMaxInput() const;
	EVisibility GetAnimControlVisibility() const;
		 

};
