// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerPreview.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Inputs/MediaSourceManagerInputMediaSource.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MediaPlayer.h"
#include "MediaSourceManagerChannel.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Subsystems/AssetEditorSubsystem.h"
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

FReply SMediaSourceManagerPreview::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		OpenContextMenu(MouseEvent);

		return FReply::Handled();
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

void SMediaSourceManagerPreview::ClearInput()
{
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		// Clear input on channel.
		Channel->Modify();
		Channel->Input = nullptr;

		// Stop player.
		UMediaPlayer* MediaPlayer = Channel->GetMediaPlayer();
		if (MediaPlayer != nullptr)
		{
			MediaPlayer->Close();
		}
	}
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

void SMediaSourceManagerPreview::OnEditInput()
{
	// Get our input.
	TArray<UObject*> AssetArray;
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		UMediaSourceManagerInput* Input = Channel->Input;
		if (Input != nullptr)
		{
			UMediaSource* MediaSource = Input->GetMediaSource();
			if (MediaSource != nullptr)
			{
				AssetArray.Add(MediaSource);
			}
		}
	}

	// Open the editor.
	if (AssetArray.Num() > 0)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAssets(AssetArray);
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

void SMediaSourceManagerPreview::OpenContextMenu(const FPointerEvent& MouseEvent)
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	// Add current asset options.
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if ((Channel != nullptr) && (Channel->Input != nullptr))
	{
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("CurrentAsset", "Current Asset"));

		// Edit.
		FUIAction EditAction(FExecuteAction::CreateSP(this, &SMediaSourceManagerPreview::OnEditInput));
		MenuBuilder.AddMenuEntry(LOCTEXT("Edit", "Edit"),
			LOCTEXT("EditToolTip", "Edit this asset"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Edit"), EditAction);

		// Clear.
		FUIAction ClearAction(FExecuteAction::CreateSP(this, &SMediaSourceManagerPreview::ClearInput));
		MenuBuilder.AddMenuEntry(LOCTEXT("Clear", "Clear"),
			LOCTEXT("ClearToolTip", "Clears the asset set on this field"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Delete"), ClearAction);

		MenuBuilder.EndSection();
	}
	
	// Bring up menu.
	FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(SharedThis(this), WidgetPath, MenuBuilder.MakeWidget(), FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect::ContextMenu);
}



#undef LOCTEXT_NAMESPACE
