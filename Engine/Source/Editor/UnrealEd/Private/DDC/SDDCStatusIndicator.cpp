// Copyright Epic Games, Inc. All Rights Reserved.

#include "DDC/SDDCStatusIndicator.h"
#include "DDC/SDDCInformation.h"
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
#include "Widgets/Images/SImage.h"
#include "Styling/StyleColors.h"

#define LOCTEXT_NAMESPACE "SDDCStatusIndicator"

void SDDCStatusIndicator::Construct(const FArguments& InArgs)
{	
	/*FDerivedDataCacheInterface::FOnDDCNotification& DDCNotificationEvent = GetDerivedDataCacheRef().GetDDCNotificationEvent();

	if (bSubscribe)
	{
		DDCNotificationEvent.AddRaw(this, &FDDCNotifications::OnDDCNotificationEvent);
	}
	else
	{
		DDCNotificationEvent.RemoveAll(this);
	}*/

	BusyPulseSequence = FCurveSequence(0.f, 1.0f, ECurveEaseFunction::QuadInOut);
	FadeGetSequence = FCurveSequence(0.f, 0.5f, ECurveEaseFunction::Linear);
	FadePutSequence = FCurveSequence(0.f, 0.5f, ECurveEaseFunction::Linear);

	this->ChildSlot
	[
		SNew(SHorizontalBox)
		.ToolTip(SNew(SToolTip)[SNew(SDDCInformation)])

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0,0,3,0)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.Settings"))
			.ColorAndOpacity_Lambda([this] { return bBusy? FLinearColor::Green.CopyWithNewOpacity(0.5f + (0.5f * FMath::MakePulsatingValue(BusyPulseSequence.GetLerp(), 1))) : FSlateColor::UseSubduedForeground(); })
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 3, 0)
		[
			SNew(SOverlay)
				
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0, 0, 4, 4)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.ArrowUp"))
				.ColorAndOpacity_Lambda([this] { return bPutActive ? FLinearColor::Green.CopyWithNewOpacity(0.5f + (0.5f * FMath::MakePulsatingValue(FadePutSequence.GetLerp(), 1))) : FSlateColor::UseSubduedForeground(); })
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(4, 4, 0, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.ArrowDown"))
				.ColorAndOpacity_Lambda([this] { return bGetActive ? FLinearColor::Green.CopyWithNewOpacity(0.5f + (0.5f * FMath::MakePulsatingValue(FadeGetSequence.GetLerp(), 1))) : FSlateColor::UseSubduedForeground(); })
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0,0,3,0)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.Local"))
			.ColorAndOpacity_Lambda([this] {
			return SDDCInformation::GetDDCHasLocalBackend() ? FStyleColors::AccentBlue : FSlateColor::UseSubduedForeground(); })
		]

		+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 3, 0)
		[
			SNew(SImage)
			.Image(FAppStyle::Get().GetBrush("Icons.Cloud"))
			.ColorAndOpacity_Lambda([this] { 
			return SDDCInformation::GetDDCHasRemoteBackend()? FStyleColors::AccentBlue : FSlateColor::UseSubduedForeground(); })
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 10, 0)
		[
			SNew(STextBlock)
			.Text_Lambda([this] { return GetInformationText(); })
		]
	];

	LastDDCGetTime = SDDCInformation::GetDDCTimeSeconds(true, false);
	LastDDCPutTime = SDDCInformation::GetDDCTimeSeconds(false, false);
	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDDCStatusIndicator::UpdateBusyIndicator));
	RegisterActiveTimer(5.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDDCStatusIndicator::UpdateWarnings));
}

EActiveTimerReturnType SDDCStatusIndicator::UpdateBusyIndicator(double InCurrentTime, float InDeltaTime)
{
	const double OldLastDDCGetTime = LastDDCGetTime;
	const double OldLastDDCPutTime = LastDDCPutTime;

	LastDDCGetTime = SDDCInformation::GetDDCTimeSeconds(true, false);
	LastDDCPutTime = SDDCInformation::GetDDCTimeSeconds(false, false);

	bGetActive = OldLastDDCGetTime != LastDDCGetTime;
	bPutActive = OldLastDDCPutTime != LastDDCPutTime;
	bBusy = GetDerivedDataCache()->AnyAsyncRequestsRemaining();

	FadeGetSequence.PlayRelative(this->AsShared(), bGetActive);
	FadePutSequence.PlayRelative(this->AsShared(), bPutActive);

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

EActiveTimerReturnType SDDCStatusIndicator::UpdateWarnings(double InCurrentTime, float InDeltaTime)
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


FText SDDCStatusIndicator::GetInformationText() const
{
	FDerivedDataCacheInterface* DDC = GetDerivedDataCache();
	const FString DDCGraphName = FName::NameToDisplayString(FString(DDC->GetGraphName()), false);
	const FText DDCLabel = FText::FromString(DDC->IsDefaultGraph() ? TEXT("DDC") : DDCGraphName);
	return DDCLabel;
}

#undef LOCTEXT_NAMESPACE