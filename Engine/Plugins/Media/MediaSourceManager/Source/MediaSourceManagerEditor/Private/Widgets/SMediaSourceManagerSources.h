// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class SVerticalBox;
class UMediaSourceManager;

/**
 * Implements the sources panel of the MediaSourceManager asset editor.
 */
class SMediaSourceManagerSources
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerSources) { }
	SLATE_END_ARGS()

	/**
	 * Construct this widget.
	 *
	 * @param InArgs					The declaration data for this widget.
	 * @param InMediaSourceManager		The manager to show details for.
	 */
	void Construct(const FArguments& InArgs, UMediaSourceManager* InMediaSourceManager);

	/**
	 * Call this to set which manager to use.
	 */
	void SetManager(UMediaSourceManager* InManager);

private:

	/** Pointer to the object that is being viewed. */
	TWeakObjectPtr<UMediaSourceManager> MediaSourceManagerPtr;

	/** Pointer to the container for the media sources. */
	TSharedPtr<SVerticalBox> ChannelsContainer;

	struct FChannelWidgets
	{
		TSharedPtr<STextBlock> InputName;
	};
	TArray<FChannelWidgets> ChannelWidgets;

	void RefreshChannels();
};
