// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AdvancedPreviewScene.h"
#include "SCommonEditorViewportToolbarBase.h"
#include "SEditorViewport.h"

class SDisplayClusterLightCardEditor;
class ADisplayClusterRootActor;

/**
 * Slate widget which renders our view client.
 */
class SDisplayClusterLightCardEditorViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SDisplayClusterLightCardEditorViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SDisplayClusterLightCardEditor> InLightCardEditor);
	~SDisplayClusterLightCardEditorViewport();

	// ICommonEditorViewportToolbarInfoProvider
	virtual TSharedRef<SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;
	// ~ICommonEditorViewportToolbarInfoProvider

	TWeakPtr<SDisplayClusterLightCardEditor> GetLightCardEditor() { return LightCardEditorPtr;}

	void SetRootActor(ADisplayClusterRootActor* NewRootActor);
	
private:
	// SEditorViewport
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> MakeViewportToolbar() override;
	virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
	virtual void BindCommands() override;
	// ~SEditorViewport

private:
	/** Preview Scene - uses advanced preview settings */
	TSharedPtr<FAdvancedPreviewScene> AdvancedPreviewScene;
	
	/** Level viewport client */
	TSharedPtr<class FDisplayClusterLightCardEditorViewportClient> ViewportClient;
	TWeakPtr<SDisplayClusterLightCardEditor> LightCardEditorPtr;

};