// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UMediaSourceManagerChannel;

/**
 * Implements a single channel.
 */
class SMediaSourceManagerChannel
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerChannel) { }
	SLATE_END_ARGS()

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 * @param InChannel					The channel to show details for.
	 */
	void Construct(const FArguments& InArgs, UMediaSourceManagerChannel* InChannel);

protected:
	// SWidget interface
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End of SWidget interface

private:

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManagerChannel> ChannelPtr;
	/** Widget for the input name. */
	TSharedPtr<STextBlock> InputNameTextBlock;
	
	/**
	 * Refreshes the widgets based on the current sstate.
	 */
	void Refresh();

};
