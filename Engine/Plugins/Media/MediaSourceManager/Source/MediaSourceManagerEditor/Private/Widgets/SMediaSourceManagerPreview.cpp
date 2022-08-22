// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerPreview.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Inputs/MediaSourceManagerInputMediaSource.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MediaSourceManagerChannel.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Widgets/SMediaPlayerEditorViewer.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerPreview"

FLazyName SMediaSourceManagerPreview::MediaTextureName("MediaTexture");

void SMediaSourceManagerPreview::Construct(const FArguments& InArg,
	UMediaSourceManagerChannel* InChannel, const TSharedRef<ISlateStyle>& InStyle)
{
	ChannelPtr = InChannel;

	UMediaPlayer* MediaPlayer = InChannel->GetMediaPlayer();
	UMediaTexture* MediaTexture = Cast<UMediaTexture>(InChannel->OutTexture);

	TSharedPtr<SMediaPlayerEditorViewer> PlayerViewer;
	ChildSlot
		[
			SAssignNew(PlayerViewer, SMediaPlayerEditorViewer, *MediaPlayer, MediaTexture, InStyle, false)
				.bShowUrl(false)
		];

	if (PlayerViewer.IsValid())
	{
		PlayerViewer->EnableMouseControl(false);
	}
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


FReply SMediaSourceManagerPreview::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	}

	return FReply::Unhandled();
}

FReply SMediaSourceManagerPreview::OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		UTexture* Texture = Channel->OutTexture;
		if (Texture != nullptr)
		{
			UMaterialInstanceConstant* Material = GetMaterial();
			if (Material != nullptr)
			{

				FAssetData AssetData(Material);
				return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(AssetData));
			}
		}
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


UMaterialInstanceConstant* SMediaSourceManagerPreview::GetMaterial()
{
	UMaterialInstanceConstant* MaterialInstance = nullptr;

	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		// Do we already have a material?
		MaterialInstance = Channel->Material;
		if (MaterialInstance == nullptr)
		{
			// No. Create one.
			UMaterial* Material = LoadObject<UMaterial>(NULL,
				TEXT("/MediaSourceManager/M_MediaSourceManager"), NULL, LOAD_None, NULL);
			if (Material != nullptr)
			{
				UObject* Outer = Channel->GetOuter();
				Channel->Modify();

				FString MaterialName = Material->GetName();
				if (MaterialName.StartsWith(TEXT("M_")))
				{
					MaterialName.InsertAt(1, TEXT("I"));
				}
				FName MaterialUniqueName = MakeUniqueObjectName(Outer, UMaterialInstanceConstant::StaticClass(),
					FName(*MaterialName));

				// Create instance.
				MaterialInstance =
					NewObject<UMaterialInstanceConstant>(Outer, MaterialUniqueName, RF_Public);
				Channel->Material = MaterialInstance;
				MaterialInstance->SetParentEditorOnly(Material);
				MaterialInstance->SetTextureParameterValueEditorOnly(
					FMaterialParameterInfo(MediaTextureName),
					Channel->OutTexture);
				MaterialInstance->PostEditChange();
			}
		}
	}

	return MaterialInstance;
}


#undef LOCTEXT_NAMESPACE
