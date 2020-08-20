// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"



/** Visual representation of a connection to a channel in a dmx universe */
class SDMXChannelConnector
	: public SCompoundWidget
{
	DECLARE_DELEGATE_TwoParams(FOnDragOverChannel, int32, const FDragDropEvent&);

	DECLARE_DELEGATE_RetVal_TwoParams(FReply, FOnDropOntoChannel, int32, const FDragDropEvent&);

public:
	SLATE_BEGIN_ARGS(SDMXChannelConnector)
		: _ChannelID(0)
		, _Value(0.0f)
		, _OnDragEnterChannel()
		, _OnDragLeaveChannel()
		, _OnDropOntoChannel()
	{}
		/** The channel ID this widget represents */
		SLATE_ARGUMENT(int32, ChannelID)

		/** The current value from the channel */
		SLATE_ATTRIBUTE(uint8, Value)

		/** Called when drag enters the widget */
		SLATE_EVENT(FOnDragOverChannel, OnDragEnterChannel)

		/** Called when drag leaves the widget */
		SLATE_EVENT(FOnDragOverChannel, OnDragLeaveChannel)

		/** Called when dropped onto the channel */
		SLATE_EVENT(FOnDropOntoChannel, OnDropOntoChannel)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	// Begin SWidget interface
	virtual void OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End SWidget interface

	TSharedPtr<class SDMXChannel> ChannelValueWidget;

	int32 Column = INDEX_NONE;
	int32 Row = INDEX_NONE;

	FOnDragOverChannel OnDragEnterChannel;
	FOnDragOverChannel OnDragLeaveChannel;
	FOnDropOntoChannel OnDropOntoChannel;
};
