// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ISlateStyle;
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
	//~ End of SWidget interface

private:

	/**
	 * Assigns a media source to this channel.
	 *
	 * @param MediaSource	Media source to use as input.
	 */
	void AssignMediaSourceInput(UMediaSource* MediaSource);

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManagerChannel> ChannelPtr;

};
