// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDerivedDataStatusBar.h"
#include "DerivedDataEditorModule.h"
#include "DerivedDataInformation.h"
#include "SDerivedDataDialogs.h"
#include "SDerivedDataCacheSettings.h"
#include "DerivedDataCacheUsageStats.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataBackendInterface.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "EditorStyleSet.h"
#include "EditorFontGlyphs.h"
#include "DerivedDataCacheUsageStats.h"
#include "Stats/Stats.h"
#include "Widgets/SOverlay.h"
#include "Settings/EditorSettings.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Async/Future.h"
#include "Settings/EditorProjectSettings.h"
#include "Modules/ModuleManager.h"
#include "ISettingsModule.h"
#include "ToolMenuContext.h"
#include "ToolMenus.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Images/SImage.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "DerivedDataEditor"


TSharedRef<FUICommandList> FDerivedDataStatusBarMenuCommands::ActionList(new FUICommandList());

FDerivedDataStatusBarMenuCommands::FDerivedDataStatusBarMenuCommands()
	: TCommands<FDerivedDataStatusBarMenuCommands>
	(
		"DerivedDataSettings",
		NSLOCTEXT("Contexts", "Derived Data", "Derived Data"),
		"LevelEditor",
		FEditorStyle::GetStyleSetName()
		)
{}

void FDerivedDataStatusBarMenuCommands::RegisterCommands()
{
	UI_COMMAND(ChangeSettings, "Change Cache Settings", "Opens a dialog to change Cache settings.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ViewCacheStatistics, "View Cache Statistics", "Opens the Cache Statistics panel.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(ViewResourceUsage, "View Resource Usage", "Opens the Resource Usage panel.", EUserInterfaceActionType::Button, FInputChord());

	ActionList->MapAction(
		ChangeSettings,
		FExecuteAction::CreateStatic(&FDerivedDataStatusBarMenuCommands::ChangeSettings_Clicked)
	);

	ActionList->MapAction(
		ViewCacheStatistics,
		FExecuteAction::CreateStatic(&FDerivedDataStatusBarMenuCommands::ViewCacheStatistics_Clicked)
	);

	ActionList->MapAction(
		ViewResourceUsage,
		FExecuteAction::CreateStatic(&FDerivedDataStatusBarMenuCommands::ViewResourceUsage_Clicked)
	);
}

void FDerivedDataStatusBarMenuCommands::ChangeSettings_Clicked()
{
	FDerivedDataEditorModule& DerivedDataEditorModule = FModuleManager::LoadModuleChecked<FDerivedDataEditorModule>("DerivedDataEditor");
	DerivedDataEditorModule.ShowSettingsDialog();
}

void FDerivedDataStatusBarMenuCommands::ViewCacheStatistics_Clicked()
{
	FDerivedDataEditorModule& DerivedDataEditorModule = FModuleManager::LoadModuleChecked<FDerivedDataEditorModule>("DerivedDataEditor");
	DerivedDataEditorModule.ShowCacheStatisticsTab();
}

void FDerivedDataStatusBarMenuCommands::ViewResourceUsage_Clicked()
{
	FDerivedDataEditorModule& DerivedDataEditorModule = FModuleManager::LoadModuleChecked<FDerivedDataEditorModule>("DerivedDataEditor");
	DerivedDataEditorModule.ShowResourceUsageTab();
}

TSharedRef<SWidget> SDerivedDataStatusBarWidget::CreateStatusBarMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->RegisterMenu("StatusBar.ToolBar.DDC", NAME_None, EMultiBoxType::Menu, false);

	/*{
		FToolMenuSection& Section = Menu->AddSection("DDCMenuSettingsSection", LOCTEXT("DDCMenuSettingsSection", "Settings"));

		Section.AddMenuEntry(
			FDerivedDataStatusBarMenuCommands::Get().ChangeSettings,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "DerivedData.Cache.Settings")
		);
	}*/

	{
		FToolMenuSection& Section = Menu->AddSection("DDCMenuStatisticsSection", LOCTEXT("DDCMenuStatisticsSection", "Statistics"));

		Section.AddMenuEntry(
			FDerivedDataStatusBarMenuCommands::Get().ViewCacheStatistics,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "DerivedData.Cache.Statistics")
		);

		Section.AddMenuEntry(
			FDerivedDataStatusBarMenuCommands::Get().ViewResourceUsage,
			TAttribute<FText>(),
			TAttribute<FText>(),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "DerivedData.ResourceUsage")
		);
	}

	return UToolMenus::Get()->GenerateWidget("StatusBar.ToolBar.DDC", FToolMenuContext(FDerivedDataStatusBarMenuCommands::ActionList));
}

void SDerivedDataStatusBarWidget::Construct(const FArguments& InArgs)
{	
	BusyPulseSequence = FCurveSequence(0.f, 1.0f, ECurveEaseFunction::QuadInOut);
	FadeGetSequence = FCurveSequence(0.f, 0.5f, ECurveEaseFunction::Linear);
	FadePutSequence = FCurveSequence(0.f, 0.5f, ECurveEaseFunction::Linear);

	this->ChildSlot
	[
		SNew(SComboButton)
		.ContentPadding(FMargin(6.0f, 0.0f))
		.MenuPlacement(MenuPlacement_AboveAnchor)
		.ComboButtonStyle(&FAppStyle::Get().GetWidgetStyle<FComboButtonStyle>("StatusBar.StatusBarComboButton"))
		.ButtonContent()
		[

			SNew(SHorizontalBox)
	
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 3, 0)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Top)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image_Lambda([this] { return GetRemoteCacheStateBackgroundIcon();  })
					.ToolTipText_Lambda([this] { return GetRemoteCacheToolTipText(); })
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Top)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image_Lambda([this] { return GetRemoteCacheStateBadgeIcon();  })
					.ToolTipText_Lambda([this] { return GetRemoteCacheToolTipText(); })
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Top)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("DerivedData.RemoteCache.Uploading"))
					.ColorAndOpacity_Lambda([this] { return FDerivedDataInformation::IsUploading() ? FLinearColor::White.CopyWithNewOpacity(0.5f + (0.5f * FMath::MakePulsatingValue(FadePutSequence.GetLerp(), 1))) : FLinearColor(0,0,0,0); })
					.ToolTipText_Lambda([this] { return GetRemoteCacheToolTipText(); })
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Bottom)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("DerivedData.RemoteCache.Downloading"))
					.ColorAndOpacity_Lambda([this] { return FDerivedDataInformation::IsDownloading() ? FLinearColor::White.CopyWithNewOpacity(0.5f + (0.5f * FMath::MakePulsatingValue(FadeGetSequence.GetLerp(), 1))) : FLinearColor(0, 0, 0, 0); })
					.ToolTipText_Lambda([this] { return GetRemoteCacheToolTipText(); })
				]				
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 10, 0)
			[
				SNew(STextBlock)
				.Text_Lambda([this] { return GetTitleText(); })
				.ToolTipText_Lambda([this] { return GetTitleToolTipText(); })
			]
		]
		.OnGetMenuContent(FOnGetContent::CreateRaw(this, &SDerivedDataStatusBarWidget::CreateStatusBarMenu))
	];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDerivedDataStatusBarWidget::UpdateBusyIndicator));
	RegisterActiveTimer(5.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDerivedDataStatusBarWidget::UpdateWarnings));
}

EActiveTimerReturnType SDerivedDataStatusBarWidget::UpdateBusyIndicator(double InCurrentTime, float InDeltaTime)
{	
	FDerivedDataInformation::UpdateRemoteCacheState();

	bBusy = GetDerivedDataCache()->AnyAsyncRequestsRemaining();

	FadeGetSequence.PlayRelative(this->AsShared(), FDerivedDataInformation::IsDownloading());
	FadePutSequence.PlayRelative(this->AsShared(), FDerivedDataInformation::IsUploading());

	if (bBusy)
	{
		if (!BusyPulseSequence.IsPlaying())
		{
			BusyPulseSequence.Play(this->AsShared(), true);
		}
	}
	else
	{
		BusyPulseSequence.JumpToEnd();
		BusyPulseSequence.Pause();
	}

	return EActiveTimerReturnType::Continue;
}

EActiveTimerReturnType SDerivedDataStatusBarWidget::UpdateWarnings(double InCurrentTime, float InDeltaTime)
{
	const UEditorSettings* Settings = GetDefault<UEditorSettings>();
	const UDDCProjectSettings* DDCProjectSettings = GetDefault<UDDCProjectSettings>();

	if (DDCProjectSettings->RecommendEveryoneSetupAGlobalLocalDDCPath && Settings->GlobalLocalDDCPath.Path.IsEmpty())
	{
		TPromise<TWeakPtr<SNotificationItem>> NotificationPromise;

		FNotificationInfo Info(LOCTEXT("SharedProjectLocalDDC", "This project recommends you setup the 'Global Local DDC Path', \nso that all copies of this project use the same local DDC cache."));
		Info.bUseSuccessFailIcons = true;
		Info.bFireAndForget = false;
		Info.bUseThrobber = false;
		Info.FadeOutDuration = 0.0f;
		Info.ExpireDuration = 0.0f;

		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("UpdateSettings", "Update Settings"), FText(), FSimpleDelegate::CreateLambda([NotificationFuture = NotificationPromise.GetFuture().Share()]() {
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "General", "Global");

			TWeakPtr<SNotificationItem> NotificationPtr = NotificationFuture.Get();
			if (TSharedPtr<SNotificationItem> Notification = NotificationPtr.Pin())
			{
				Notification->SetCompletionState(SNotificationItem::CS_None);
				Notification->ExpireAndFadeout();
			}
		}),
		SNotificationItem::ECompletionState::CS_Fail));

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationItem.IsValid())
		{
			NotificationPromise.SetValue(NotificationItem);
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	if (DDCProjectSettings->RecommendEveryoneSetupAGlobalS3DDCPath && (Settings->bEnableS3DDC && Settings->GlobalS3DDCPath.Path.IsEmpty()))
	{
		TPromise<TWeakPtr<SNotificationItem>> NotificationPromise;

		FNotificationInfo Info(LOCTEXT("SharedProjectS3DDC", "This project recommends you setup the 'Global Local S3 DDC Path', \nso that all copies of this project use the same local S3 DDC cache."));
		Info.bUseSuccessFailIcons = true;
		Info.bFireAndForget = false;
		Info.bUseThrobber = false;
		Info.FadeOutDuration = 0.0f;
		Info.ExpireDuration = 0.0f;

		Info.ButtonDetails.Add(FNotificationButtonInfo(LOCTEXT("UpdateSettings", "Update Settings"), FText(), FSimpleDelegate::CreateLambda([NotificationFuture = NotificationPromise.GetFuture().Share()]() {
			FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Editor", "General", "Global");

			TWeakPtr<SNotificationItem> NotificationPtr = NotificationFuture.Get();
			if (TSharedPtr<SNotificationItem> Notification = NotificationPtr.Pin())
			{
				Notification->SetCompletionState(SNotificationItem::CS_None);
				Notification->ExpireAndFadeout();
			}
		}),
		SNotificationItem::ECompletionState::CS_Fail));

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationItem.IsValid())
		{
			NotificationPromise.SetValue(NotificationItem);
			NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}

	return EActiveTimerReturnType::Stop;
}

FText SDerivedDataStatusBarWidget::GetTitleToolTipText() const
{
	FTextBuilder DescBuilder;

	DescBuilder.AppendLineFormat(LOCTEXT("GraphNameText", "Graph : {0}"), FText::FromString(GetDerivedDataCache()->GetGraphName()));
	
	return DescBuilder.ToText();
}

FText SDerivedDataStatusBarWidget::GetTitleText() const
{
	return LOCTEXT("DerivedDataToolBarName", "Derived Data");
}

FText GetRemoteCacheStateAsText()
{
	switch ( FDerivedDataInformation::GetRemoteCacheState())
	{
		case ERemoteCacheState::Idle :
		{
			return FText::FromString(TEXT("Idle"));
			break;
		}

		case ERemoteCacheState::Busy :
		{
			return FText::FromString(TEXT("Busy"));
			break;
		}	

		case ERemoteCacheState::Unavailable:
		{
			return FText::FromString(TEXT("Unavailable"));
			break;
		}

		default:
		case ERemoteCacheState::Warning:
		{
			return FText::FromString(TEXT("Warning"));
			break;
		}
	}
}

FText SDerivedDataStatusBarWidget::GetRemoteCacheToolTipText() const
{
	FTextBuilder DescBuilder;

	DescBuilder.AppendLine(LOCTEXT("RemoteCacheToolTipText", "Remote Cache\n"));
	DescBuilder.AppendLineFormat(LOCTEXT("RemoteCacheConnectedText", "Connected\t: {0}"), FText::FromString((FDerivedDataInformation::GetHasRemoteCache() ? TEXT("Yes") : TEXT("No"))));
	DescBuilder.AppendLineFormat(LOCTEXT("RemoteCacheStatusText", "Status\t: {0}"), GetRemoteCacheStateAsText() );
	
	const double DownloadedBytesMB = FUnitConversion::Convert(FDerivedDataInformation::GetCacheActivitySizeBytes(true, false), EUnit::Bytes, EUnit::Megabytes);
	const double UploadedBytesMB = FUnitConversion::Convert(FDerivedDataInformation::GetCacheActivitySizeBytes(false, false), EUnit::Bytes, EUnit::Megabytes);

	DescBuilder.AppendLineFormat(LOCTEXT("RemoteCacheDownloaded", "Downloaded\t: {0} MB"), DownloadedBytesMB);
	DescBuilder.AppendLineFormat(LOCTEXT("RemoteCacheUploaded",	"Uploaded\t: {0} MB"), UploadedBytesMB);
	
	return DescBuilder.ToText();
}

const FSlateBrush* SDerivedDataStatusBarWidget::GetRemoteCacheStateBackgroundIcon() const
{
	switch ( FDerivedDataInformation::GetRemoteCacheState())
	{
		case ERemoteCacheState::Idle :
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.IdleBG");
			break;
		}

		case ERemoteCacheState::Busy :
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.BusyBG");
			break;
		}	

		case ERemoteCacheState::Unavailable:
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.UnavailableBG");
			break;
		}

		default:
		case ERemoteCacheState::Warning:
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.WarningBG");
			break;
		}
	}
}

const FSlateBrush* SDerivedDataStatusBarWidget::GetRemoteCacheStateBadgeIcon() const
{
	switch ( FDerivedDataInformation::GetRemoteCacheState())
	{
		case ERemoteCacheState::Idle :
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.Idle");
			break;
		}

		case ERemoteCacheState::Busy :
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.Busy");
			break;
		}	

		case ERemoteCacheState::Unavailable:
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.Unavailable");
			break;
		}

		default:
		case ERemoteCacheState::Warning:
		{
			return FAppStyle::Get().GetBrush("DerivedData.RemoteCache.Warning");
			break;
		}
	}
}

#undef LOCTEXT_NAMESPACE