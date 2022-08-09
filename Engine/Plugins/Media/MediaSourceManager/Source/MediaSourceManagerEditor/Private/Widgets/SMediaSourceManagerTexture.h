// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UMaterialInstanceConstant;
class UMediaSourceManagerChannel;

/**
 * Handles a single texture.
 */
class SMediaSourceManagerTexture
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerTexture) { }
	SLATE_END_ARGS()

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 * @param InChannel					The channel to use.
	 */
	void Construct(const FArguments& InArgs, UMediaSourceManagerChannel* InChannel);

protected:
	// SWidget interface
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	// End of SWidget interface

private:
	/**
	 * Gets a material for this texture.
	 */
	UMaterialInstanceConstant* GetMaterial();

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManagerChannel> ChannelPtr;

	/** Name for the media texture parameter in the material. */
	static FLazyName MediaTextureName;
};
