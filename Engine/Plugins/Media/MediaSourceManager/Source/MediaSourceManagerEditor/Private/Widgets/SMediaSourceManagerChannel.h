// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class IMediaIOCoreDeviceProvider;
class SNotificationItem;
class STextBlock;
class UMediaSource;
class UMediaSourceManagerChannel;

struct FAssetData;
struct FMediaIODevice;

/**
 * Implements a single channel.
 */
class SMediaSourceManagerChannel
	: public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMediaSourceManagerChannel) { }
	SLATE_END_ARGS()

	virtual ~SMediaSourceManagerChannel();

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
	/** Holds our error notification so we can dismiss it. */
	TWeakPtr<SNotificationItem> ErrorNotificationPtr;
	
	/**
	 * Callback to create the assign input menu.
	 */
	TSharedRef<SWidget> CreateAssignInputMenu();

	/**
	 * Creates a widget to select a media source.
	 */
	TSharedRef<SWidget> BuildMediaSourcePickerWidget();

	/**
	 * Callback to add a media source.
	 */
	void AddMediaSource(const FAssetData& AssetData);

	/**
	 * Callback to add a media source.
	 */
	void AddMediaSourceEnterPressed(const TArray<FAssetData>& AssetData);

	/**
	 * Assignss a media source to this channel.
	 * 
	 * @param MediaSource	Media source to use as input.
	 */
	void AssignMediaSourceInput(UMediaSource* MediaSource);

	/**
	 * Assigns a MediaIO input to this channel.
	 *
	 * @param DeviceProvider	MediaIO device provider to get the input from.
	 * @param Device			Device to pass to the device provider.
	 */
	void AssignMediaIOInput(IMediaIOCoreDeviceProvider* DeviceProvider, FMediaIODevice Device);

	/**
	 * Call this to remove the error notification.
	 */
	void DismissErrorNotification();

	/**
	 * Called when the edit input button is pressed.
	 */
	FReply OnEditInput();

	/**
	 * Refreshes the widgets based on the current sstate.
	 */
	void Refresh();

};
