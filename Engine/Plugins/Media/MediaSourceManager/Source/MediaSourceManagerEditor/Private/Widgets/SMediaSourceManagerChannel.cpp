// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerChannel.h"

#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IMediaIOCoreDeviceProvider.h"
#include "IMediaIOCoreModule.h"
#include "Inputs/MediaSourceManagerInputMediaSource.h"
#include "MediaSource.h"
#include "MediaSourceManagerChannel.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SMediaSourceManagerTexture.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMediaSourceManagerChannel"

SMediaSourceManagerChannel::~SMediaSourceManagerChannel()
{
	DismissErrorNotification();
}

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

			// Set input.
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2)
				.HAlign(HAlign_Left)
				[
					SNew(SComboButton)
						.OnGetMenuContent(this, &SMediaSourceManagerChannel::CreateAssignInputMenu)
						.ContentPadding(2)
						.ButtonContent()
						[
							SNew(STextBlock)
								.ToolTipText(LOCTEXT("Assign_ToolTip",
									"Assign an input to this channel."))
								.Text(LOCTEXT("AssignInput", "Assign Input"))
						]
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


TSharedRef<SWidget> SMediaSourceManagerChannel::CreateAssignInputMenu()
{
	FMenuBuilder MenuBuilder(true, NULL);

	// Get all Media IO device providers.
	IMediaIOCoreModule& MediaIOCoreModule = IMediaIOCoreModule::Get();
	TConstArrayView<IMediaIOCoreDeviceProvider*> DeviceProviders =
		MediaIOCoreModule.GetDeviceProviders();

	// Loop through each provider.
	for (IMediaIOCoreDeviceProvider* DeviceProvider : DeviceProviders)
	{
		if (DeviceProvider != nullptr)
		{
			// Start menu section.
			FName ProviderName = DeviceProvider->GetFName();
			MenuBuilder.BeginSection(ProviderName, FText::FromName(ProviderName));

			// Go over all input configurations.
			TArray<FMediaIOConfiguration> Configurations = DeviceProvider->GetConfigurations(true, false);
			for (const FMediaIOConfiguration& Configuration : Configurations)
			{
				// Add this device.
				FName DeviceName = Configuration.MediaConnection.Device.DeviceName;

				FUIAction AssignMediaIOInputAction(FExecuteAction::CreateSP(this,
					&SMediaSourceManagerChannel::AssignMediaIOInput, DeviceProvider, Configuration));
				MenuBuilder.AddMenuEntry(FText::FromName(DeviceName), FText(), FSlateIcon(),
					AssignMediaIOInputAction);
			}

			MenuBuilder.EndSection();
		}
	}

	return MenuBuilder.MakeWidget();
}

void SMediaSourceManagerChannel::AssignMediaIOInput(IMediaIOCoreDeviceProvider* DeviceProvider,
	FMediaIOConfiguration Config)
{
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		// Create media source.
		UMediaSource* MediaSource = DeviceProvider->CreateMediaSource(Config, Channel);
		if (MediaSource != nullptr)
		{
			// Assign to channel.
			Channel->Modify();
			UMediaSourceManagerInputMediaSource* Input = NewObject<UMediaSourceManagerInputMediaSource>(Channel);
			Input->MediaSource = MediaSource;
			Channel->Input = Input;

			Channel->Play();

			Refresh();
		}
		else
		{
			// Failed to create media source.
			// Remove any existing error.
			DismissErrorNotification();

			// Inform the user.
			FNotificationInfo Info(LOCTEXT("FailedToCreateMediaSource", "Failed to create a Media Source."));
			Info.bFireAndForget = false;
			Info.bUseLargeFont = false;
			Info.bUseThrobber = false;
			Info.FadeOutDuration = 0.25f;
			Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("Dismiss", "Dismiss"), LOCTEXT("DismissToolTip", "Dismiss this notification."),
				FSimpleDelegate::CreateSP(this, &SMediaSourceManagerChannel::DismissErrorNotification)));

			ErrorNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
			TSharedPtr<SNotificationItem> ErrorNotification = ErrorNotificationPtr.Pin();
			if (ErrorNotification != nullptr)
			{
				ErrorNotification->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}
	}
}

void SMediaSourceManagerChannel::DismissErrorNotification()
{
	TSharedPtr<SNotificationItem> ErrorNotification = ErrorNotificationPtr.Pin();
	if (ErrorNotification != nullptr)
	{
		ErrorNotification->ExpireAndFadeout();
		ErrorNotification.Reset();
	}
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
