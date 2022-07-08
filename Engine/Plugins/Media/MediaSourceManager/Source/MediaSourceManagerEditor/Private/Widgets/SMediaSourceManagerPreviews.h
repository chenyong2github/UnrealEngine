// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ISlateStyle;
class SHorizontalBox;
class UMediaSourceManager;

/**
 * Implements a collection of previews for the media source manager.
 */
class SMediaSourceManagerPreviews
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerPreviews) { }
	SLATE_END_ARGS()
	~SMediaSourceManagerPreviews();

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 */
	void Construct(const FArguments& InArgs);

private:
	/**
	 * Call this to use the manager in the settings.
	 */
	void SetManager();

	/**
	 * Update the channel widgets to reflect the channels in the current manager.
	 */
	void RefreshChannels();

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManager> MediaSourceManagerPtr;

	/** Pointer to the container for the channels. */
	TSharedPtr<SHorizontalBox> ChannelsContainer;
	/** Style to pass to the widgets. */
	TSharedPtr<ISlateStyle> Style;
};
