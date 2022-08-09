// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerTexture.h"

#include "AssetRegistry/AssetData.h"
#include "AssetThumbnail.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MediaSourceManagerChannel.h"
#include "ThumbnailRendering/ThumbnailManager.h"

FLazyName SMediaSourceManagerTexture::MediaTextureName("MediaTexture");

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

UMaterialInstanceConstant* SMediaSourceManagerTexture::GetMaterial()
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
