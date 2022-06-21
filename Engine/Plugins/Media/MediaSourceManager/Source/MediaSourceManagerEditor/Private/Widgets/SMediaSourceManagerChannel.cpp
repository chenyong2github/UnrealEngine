// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerChannel.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Inputs/MediaSourceManagerInputMediaSource.h"
#include "MediaSource.h"
#include "MediaSourceManagerChannel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SMediaSourceManagerTexture.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerChannel"

void SMediaSourceManagerChannel::Construct(const FArguments& InArgs,
	UMediaSourceManagerChannel* InChannel)
{
	ChannelPtr = InChannel;

	ChildSlot
		[
			SNew(SHorizontalBox)

			// Name of channel.
			+ SHorizontalBox::Slot()
				.FillWidth(0.11f)
				.Padding(2)
				.HAlign(HAlign_Left)
				[
					SNew(STextBlock)
						.Text(FText::FromString(ChannelPtr->Name))
				]

			// Name of input.
			+ SHorizontalBox::Slot()
				.FillWidth(0.11f)
				.Padding(2)
				.HAlign(HAlign_Left)
				[
					SAssignNew(InputNameTextBlock, STextBlock)
				]

			// Out texture
			+ SHorizontalBox::Slot()
				.FillWidth(0.11f)
				.Padding(2)
				.HAlign(HAlign_Left)
				[
					SNew(SMediaSourceManagerTexture, ChannelPtr.Get())
				]
		];

	Refresh();

	// Start playing.
	if (ChannelPtr != nullptr)
	{
		ChannelPtr->Play();
	}
}

void SMediaSourceManagerChannel::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
}

void SMediaSourceManagerChannel::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
}

FReply SMediaSourceManagerChannel::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Is this an asset drop?
	if (TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SMediaSourceManagerChannel::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Is this an asset drop?
	if (TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
		if (Channel != nullptr)
		{
			for (const FAssetData& Asset : AssetDragDrop->GetAssets())
			{
				// Is this a media source?
				UMediaSource* MediaSource = Cast<UMediaSource>(Asset.GetAsset());
				if (MediaSource != nullptr)
				{
					Channel->Modify();
					UMediaSourceManagerInputMediaSource* Input = NewObject<UMediaSourceManagerInputMediaSource>(Channel);
					Input->MediaSource = MediaSource;
					Channel->Input = Input;

					Channel->Play();
						
					Refresh();
					break;
				}
			}
		}
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

void SMediaSourceManagerChannel::Refresh()
{
	// Get channel.
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		FString InputName = FString(TEXT("None"));

		// Get input.
		UMediaSourceManagerInput* Input = Channel->Input;
		if (Input != nullptr)
		{
			InputName = Input->GetDisplayName();
		}

		// Update input widgets.
		InputNameTextBlock->SetText(FText::FromString(InputName));
	}
}


#undef LOCTEXT_NAMESPACE
