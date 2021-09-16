// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/GCObject.h"
#include "UObject/StrongObjectPtr.h"

class UTexture;

struct FGeometry;
struct FPointerEvent;

class SSimulcamEditorViewport;

/** Delegate to be executed when the viewport area is clicked */
DECLARE_DELEGATE_TwoParams(FSimulcamViewportClickedEventHandler, const FGeometry& MyGeometry, const FPointerEvent& PointerEvent);

/**
 * UI to display the provided UTexture and respond to mouse clicks. 
 */
class SSimulcamViewport : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SSimulcamViewport)
	: _WithZoom(true), _WithPan(true)
	{}
	SLATE_EVENT(FSimulcamViewportClickedEventHandler, OnSimulcamViewportClicked)
	SLATE_ATTRIBUTE(bool, WithZoom)
	SLATE_ATTRIBUTE(bool, WithPan)
	SLATE_END_ARGS()

	/**
	 * Construct this widget
	 *
	 * @param InArgs The declaration data for this widget.
	 * @param InTexture The source texture to render if any
	 */
	void Construct(const FArguments& InArgs, UTexture* InTexture);

	/** Returns the current camera texture */
	UTexture* GetTexture() const;

	/** Is the camera texture valid ? */
	bool HasValidTextureResource() const;

	/** called whenever the viewport is clicked */
	void OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& PointerEvent) { OnSimulcamViewportClicked.ExecuteIfBound(MyGeometry, PointerEvent); }

private:

	/** Delegate to be executed when the viewport area is clicked */
	FSimulcamViewportClickedEventHandler OnSimulcamViewportClicked;

	TSharedPtr<SSimulcamEditorViewport> TextureViewport;

	TStrongObjectPtr<UTexture> Texture;
};

