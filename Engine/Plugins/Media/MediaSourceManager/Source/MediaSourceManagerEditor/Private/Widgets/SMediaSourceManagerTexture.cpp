// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerTexture.h"

#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Engine/Texture.h"
#include "MediaSourceManagerChannel.h"
#include "ThumbnailRendering/ThumbnailManager.h"

void SMediaSourceManagerTexture::Construct(const FArguments& InArgs,
	UMediaSourceManagerChannel* InChannel)
{
	ChannelPtr = InChannel;

	// Set up texture thumbnail.
	UTexture* Texture = nullptr;
	if (ChannelPtr.IsValid())
	{
		Texture = ChannelPtr->OutTexture;
	}
	FAssetData ThumbnailAssetData = Texture;
	TSharedPtr<FAssetThumbnail> Thumbnail = MakeShareable(new FAssetThumbnail(ThumbnailAssetData,
		64, 64, UThumbnailManager::Get().GetSharedThumbnailPool()));

	ChildSlot
		[
			Thumbnail->MakeThumbnailWidget()
		];
}

FReply SMediaSourceManagerTexture::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}

	return FReply::Unhandled();
}

FReply SMediaSourceManagerTexture::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		UTexture* Texture = Channel->OutTexture;
		if (Texture != nullptr)
		{
			FAssetData AssetData(Texture);
			return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(AssetData));
		}
	}

	return FReply::Unhandled();
}
