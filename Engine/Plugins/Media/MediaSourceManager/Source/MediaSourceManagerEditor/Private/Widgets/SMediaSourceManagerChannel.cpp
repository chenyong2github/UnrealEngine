// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerChannel.h"

#include "ContentBrowserModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IContentBrowserSingleton.h"
#include "IMediaIOCoreDeviceProvider.h"
#include "IMediaIOCoreModule.h"
#include "Inputs/MediaSourceManagerInputMediaSource.h"
#include "MediaSource.h"
#include "MediaSourceManagerChannel.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
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
				.FillWidth(0.6f)
				.Padding(2)
				.HAlign(HAlign_Left)
				[
					SNew(SComboButton)
						.OnGetMenuContent(this, &SMediaSourceManagerChannel::CreateAssignInputMenu)
						.ContentPadding(2)
						.ButtonContent()
						[
							SAssignNew(InputNameTextBlock, STextBlock)
								.ToolTipText(LOCTEXT("Assign_ToolTip",
									"Assign an input to this channel."))
						]
				]

			// Edit input.
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.Padding(2)
				[
					SNew(SButton)
						.ContentPadding(3)
						.OnClicked(this, &SMediaSourceManagerChannel::OnEditInput)
						.Text(LOCTEXT("EditInput", "Edit Input"))
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

			// Go over all devices.
			TArray<FMediaIODevice> Devices = DeviceProvider->GetDevices();
			for (const FMediaIODevice& Device : Devices)
			{
				// Add this device.
				FName DeviceName = Device.DeviceName;

				FUIAction AssignMediaIOInputAction(FExecuteAction::CreateSP(this,
					&SMediaSourceManagerChannel::AssignMediaIOInput, DeviceProvider, Device));
				MenuBuilder.AddMenuEntry(FText::FromName(DeviceName), FText(), FSlateIcon(),
					AssignMediaIOInputAction);
			}

			MenuBuilder.EndSection();
		}
	}

	// Add assets.
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("MediaSourceAssets", "Media Source Assets"));
	auto SubMenuCallback = [this](FMenuBuilder& SubMenuBuilder)
	{
		SubMenuBuilder.AddWidget(BuildMediaSourcePickerWidget(), FText::GetEmpty(), true);
	};
	MenuBuilder.AddSubMenu(
		LOCTEXT("SelectAsset", "Select Asset"),
		LOCTEXT("SelectAsset_ToolTip", "Select an existing Media Source asset."),
		FNewMenuDelegate::CreateLambda(SubMenuCallback)
	);
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> SMediaSourceManagerChannel::BuildMediaSourcePickerWidget()
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this,
			&SMediaSourceManagerChannel::AddMediaSource);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this,
			&SMediaSourceManagerChannel::AddMediaSourceEnterPressed);
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.Filter.bRecursiveClasses = true;
		AssetPickerConfig.Filter.ClassPaths.Add(UMediaSource::StaticClass()->GetClassPathName());
		AssetPickerConfig.SaveSettingsName = TEXT("MediaSourceManagerAssetPicker");
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedRef<SBox> Picker = SNew(SBox)
		.WidthOverride(300.0f)
		.HeightOverride(300.f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];

	return Picker;
}

void SMediaSourceManagerChannel::AddMediaSource(const FAssetData& AssetData)
{
	FSlateApplication::Get().DismissAllMenus();

	// Get media source from the asset..
	UObject* SelectedObject = AssetData.GetAsset();
	if (SelectedObject)
	{
		UMediaSource* MediaSource = Cast<UMediaSource>(AssetData.GetAsset());
		if (MediaSource != nullptr)
		{
			AssignMediaSourceInput(MediaSource);
		}
	}
}

void SMediaSourceManagerChannel::AddMediaSourceEnterPressed(const TArray<FAssetData>& AssetData)
{
	if (AssetData.Num() > 0)
	{
		AddMediaSource(AssetData[0].GetAsset());
	}
}


void SMediaSourceManagerChannel::AssignMediaSourceInput(UMediaSource* MediaSource)
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

		Refresh();
	}
}

void SMediaSourceManagerChannel::AssignMediaIOInput(IMediaIOCoreDeviceProvider* DeviceProvider,
	FMediaIODevice Device)
{
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		// Create media source.
		FMediaIOConfiguration Configuration;
		Configuration = DeviceProvider->GetDefaultConfiguration();
		Configuration.MediaConnection.Device = Device;
		UMediaSource* MediaSource = DeviceProvider->CreateMediaSource(Configuration, Channel);
		if (MediaSource != nullptr)
		{
			AssignMediaSourceInput(MediaSource);
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


FReply SMediaSourceManagerChannel::OnEditInput()
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

	return FReply::Handled();
}

void SMediaSourceManagerChannel::Refresh()
{
	// Get channel.
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		FText InputName = LOCTEXT("AssignInput", "Assign Input");

		// Get input.
		UMediaSourceManagerInput* Input = Channel->Input;
		if (Input != nullptr)
		{
			InputName = FText::FromString(Input->GetDisplayName());
		}

		// Update input widgets.
		InputNameTextBlock->SetText(InputName);
	}
}


#undef LOCTEXT_NAMESPACE
