// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXChannelConnector.h"

#include "Widgets/SDMXChannel.h"


void SDMXChannelConnector::Construct(const FArguments& InArgs)
{
	OnDragEnterChannel = InArgs._OnDragEnterChannel;
	OnDragLeaveChannel = InArgs._OnDragLeaveChannel;
	OnDropOntoChannel = InArgs._OnDropOntoChannel;

	ChildSlot
		[
			SAssignNew(ChannelValueWidget, SDMXChannel)
			.ID(InArgs._ChannelID)
			.Value(InArgs._Value)	
			.bShowChannelIDBottom(true)
		];
}

void SDMXChannelConnector::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	OnDragEnterChannel.ExecuteIfBound(ChannelValueWidget->GetID(), DragDropEvent);
}

void SDMXChannelConnector::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	OnDragLeaveChannel.ExecuteIfBound(ChannelValueWidget->GetID(), DragDropEvent);
}

FReply SDMXChannelConnector::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return OnDropOntoChannel.Execute(ChannelValueWidget->GetID(), DragDropEvent);
}
