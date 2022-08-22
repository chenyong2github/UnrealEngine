// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SMediaSourceManagerPreview.h"

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
#include "Materials/MaterialInstanceConstant.h"
#include "MediaIOPermutationsSelectorBuilder.h"
#include "MediaPlayer.h"
#include "MediaSourceManagerChannel.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Notifications/SNotificationList.h"
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

void SMediaSourceManagerPreview::AssignMediaIOInput(IMediaIOCoreDeviceProvider* DeviceProvider,
	FMediaIOConnection Connection)
{
	UMediaSourceManagerChannel* Channel = ChannelPtr.Get();
	if (Channel != nullptr)
	{
		// Create media source.
		FMediaIOConfiguration Configuration;
		Configuration = DeviceProvider->GetDefaultConfiguration();
		Configuration.MediaConnection = Connection;
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
				FSimpleDelegate::CreateSP(this, &SMediaSourceManagerPreview::DismissErrorNotification)));

			ErrorNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
			TSharedPtr<SNotificationItem> ErrorNotification = ErrorNotificationPtr.Pin();
			if (ErrorNotification != nullptr)
			{
				ErrorNotification->SetCompletionState(SNotificationItem::CS_Pending);
			}
		}
	}
}

TSharedRef<SWidget> SMediaSourceManagerPreview::BuildMediaSourcePickerWidget()
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this,
			&SMediaSourceManagerPreview::AddMediaSource);
		AssetPickerConfig.OnAssetEnterPressed = FOnAssetEnterPressed::CreateRaw(this,
			&SMediaSourceManagerPreview::AddMediaSourceEnterPressed);
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

void SMediaSourceManagerPreview::AddMediaSource(const FAssetData& AssetData)
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

void SMediaSourceManagerPreview::AddMediaSourceEnterPressed(const TArray<FAssetData>& AssetData)
{
	if (AssetData.Num() > 0)
	{
		AddMediaSource(AssetData[0].GetAsset());
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

			// Go over all connections.
			TArray<FMediaIOConnection> Connections = DeviceProvider->GetConnections();
			for (const FMediaIOConnection& Connection : Connections)
			{
				// Add this connection.
				FText DeviceName = FText::FromName(Connection.Device.DeviceName);
				FText LinkName = FMediaIOPermutationsSelectorBuilder::GetLabel(
					FMediaIOPermutationsSelectorBuilder::NAME_TransportType,
					Connection);
				FText MenuText;
				if (DeviceProvider->ShowInputTransportInSelector())
				{
					MenuText = FText::Format(LOCTEXT("Connection", "{0}: {1}"),
						DeviceName, LinkName);
				}
				else
				{
					MenuText = DeviceName;
				}

				FUIAction AssignMediaIOInputAction(FExecuteAction::CreateSP(this,
					&SMediaSourceManagerPreview::AssignMediaIOInput, DeviceProvider, Connection));
				MenuBuilder.AddMenuEntry(MenuText, FText(), FSlateIcon(),
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
	
	// Bring up menu.
	FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(SharedThis(this), WidgetPath, MenuBuilder.MakeWidget(), FSlateApplication::Get().GetCursorPos(), FPopupTransitionEffect::ContextMenu);
}

void SMediaSourceManagerPreview::DismissErrorNotification()
{
	TSharedPtr<SNotificationItem> ErrorNotification = ErrorNotificationPtr.Pin();
	if (ErrorNotification != nullptr)
	{
		ErrorNotification->ExpireAndFadeout();
		ErrorNotification.Reset();
	}
}

#undef LOCTEXT_NAMESPACE
