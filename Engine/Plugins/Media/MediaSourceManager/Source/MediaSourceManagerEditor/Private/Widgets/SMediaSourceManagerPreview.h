// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ISlateStyle;
class UMaterialInstanceConstant;
class UMediaSource;
class UMediaSourceManagerChannel;

/**
 * Implements a preview for a single media source manager channel.
 */
class SMediaSourceManagerPreview
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerPreview) { }
	SLATE_END_ARGS()

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 * @param InChannel					The channel to preview.
	 * @param InStyle					The style set to use.
	 */
	void Construct(const FArguments& InArgs, UMediaSourceManagerChannel* InChannel,
		const TSharedRef<ISlateStyle>& InStyle);

	//~ SWidget interface
	virtual void OnDragEnter(const FGeometry& MyGeometrsy, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ End of SWidget interface

private:

	/**
	 * Clears the input on this channel.
	 */
	void ClearInput();

	/**
	 * Assigns a media source to this channel.
	 *
	 * @param MediaSource	Media source to use as input.
	 */
	void AssignMediaSourceInput(UMediaSource* MediaSource);

	/**
	 * Call this to bring up an editor to edit the input.
	 */
	void OnEditInput();

	/**
	 * Gets a material for this texture.
	 */
	UMaterialInstanceConstant* GetMaterial();

	/**
	 * Call this to open a context menu.
	 */
	void OpenContextMenu(const FPointerEvent& MouseEvent);

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManagerChannel> ChannelPtr;

	/** Name for the media texture parameter in the material. */
	static FLazyName MediaTextureName;

};
