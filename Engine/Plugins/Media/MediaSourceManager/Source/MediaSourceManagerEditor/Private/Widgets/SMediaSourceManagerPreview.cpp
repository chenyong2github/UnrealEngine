// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerPreview.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Inputs/MediaSourceManagerInputMediaSource.h"
#include "MediaSourceManagerChannel.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Widgets/SMediaPlayerEditorViewer.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerPreview"

void SMediaSourceManagerPreview::Construct(const FArguments& InArg,
	UMediaSourceManagerChannel* InChannel, const TSharedRef<ISlateStyle>& InStyle)
{
	ChannelPtr = InChannel;

	UMediaPlayer* MediaPlayer = InChannel->GetMediaPlayer();
	UMediaTexture* MediaTexture = Cast<UMediaTexture>(InChannel->OutTexture);

	ChildSlot
		[
			SNew(SMediaPlayerEditorViewer, *MediaPlayer, MediaTexture, InStyle, false)
				.bShowUrl(false)
		];
}


void SMediaSourceManagerPreview::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
}

void SMediaSourceManagerPreview::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
}

FReply SMediaSourceManagerPreview::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Is this an asset drop?
	if (TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		return FReply::Handled();
	}

	return FReply::Unhandled();
}

FReply SMediaSourceManagerPreview::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// Is this an asset drop?
	if (TSharedPtr<FAssetDragDropOp> AssetDragDrop = DragDropEvent.GetOperationAs<FAssetDragDropOp>())
	{
		for (const FAssetData& Asset : AssetDragDrop->GetAssets())
		{
			// Is this a media source?
			UMediaSource* MediaSource = Cast<UMediaSource>(Asset.GetAsset());
			if (MediaSource != nullptr)
			{
				AssignMediaSourceInput(MediaSource);
				break;
			}
		}

		return FReply::Handled();
	}

	return FReply::Unhandled();
}


void SMediaSourceManagerPreview::AssignMediaSourceInput(UMediaSource* MediaSource)
{
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		// Assign to channel.
		Channel->Modify();
		UMediaSourceManagerInputMediaSource* Input = NewObject<UMediaSourceManagerInputMediaSource>(Channel);
		Input->MediaSource = MediaSource;
		Channel->Input = Input;
		Channel->Play();
	}
}


#undef LOCTEXT_NAMESPACE
