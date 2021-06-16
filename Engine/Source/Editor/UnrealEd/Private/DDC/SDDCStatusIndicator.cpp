// Copyright Epic Games, Inc. All Rights Reserved.

#include "DDC/SDDCStatusIndicator.h"
#include "DDC/SDDCInformation.h"
#include "DerivedDataCacheInterface.h"
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

#define LOCTEXT_NAMESPACE "SDDCStatusIndicator"

void SDDCStatusIndicator::Construct(const FArguments& InArgs)
{
	FDerivedDataCacheInterface* DDC = GetDerivedDataCache();
	const FString DDCGraphName = FName::NameToDisplayString(FString(DDC->GetGraphName()), false);
	const FText DDCLabel = FText::FromString(DDC->IsDefaultGraph() ? TEXT("DDC") : DDCGraphName);
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
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("SProjectBadge.BadgeShape"))
		.Padding(FMargin(10.0f, 4.0f))
		.BorderBackgroundColor(FColor::Black)
		.ToolTip(SNew(SToolTip)[ SNew(SDDCInformation) ])
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
				.Padding(0, 0, 4, 4)
				[
					SNew(STextBlock)
					.ColorAndOpacity_Lambda([this] { return FLinearColor::Red.CopyWithNewOpacity(FadePutSequence.GetLerp());} )
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
					.Text(FEditorFontGlyphs::Arrow_Up)
				]

				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Bottom)
				.Padding(4, 4, 0, 0)
				[
					SNew(STextBlock)
					.ColorAndOpacity_Lambda([this] { return FLinearColor::Green.CopyWithNewOpacity(FadeGetSequence.GetLerp()); })
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.8"))
					.Text(FEditorFontGlyphs::Arrow_Down)
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0,0,3,0)
			[
				SNew(STextBlock)
				.ColorAndOpacity_Lambda([this] {
					return FLinearColor::White.CopyWithNewOpacity(0.5f + (0.5f * FMath::MakePulsatingValue(BusyPulseSequence.GetLerp(), 1)));
				})
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.12"))
				.Text(FEditorFontGlyphs::Server)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 10, 0)
			[
				SNew(STextBlock)
				.Text(DDCLabel)
			]
		]
	];

	LastDDCGetTime = GetDDCTime(true);
	LastDDCPutTime = GetDDCTime(false);
	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SDDCStatusIndicator::UpdateBusyIndicator));
	RegisterActiveTimer(5.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDDCStatusIndicator::UpdateWarnings));
}

EActiveTimerReturnType SDDCStatusIndicator::UpdateBusyIndicator(double InCurrentTime, float InDeltaTime)
{
	const double OldLastDDCGetTime = LastDDCGetTime;
	const double OldLastDDCPutTime = LastDDCPutTime;

	LastDDCGetTime = GetDDCTime(true);
	LastDDCPutTime = GetDDCTime(false);

	FadeGetSequence.PlayRelative(this->AsShared(), OldLastDDCGetTime != LastDDCGetTime);
	FadePutSequence.PlayRelative(this->AsShared(), OldLastDDCPutTime != LastDDCPutTime);

	if (GetDerivedDataCache()->AnyAsyncRequestsRemaining())
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

double SDDCStatusIndicator::GetDDCTime(bool bGet) const
{
	int64 TotalCycles = 0;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TSharedRef<FDerivedDataCacheStatsNode> RootUsage = GetDerivedDataCache()->GatherUsageStats();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	for (const auto& KVP : RootUsage->Stats)
	{
		const FDerivedDataCacheUsageStats& Stats = KVP.Value;
		
		if (bGet)
		{
			TotalCycles +=
				(Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
				Stats.GetStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));
		}
		else
		{
			TotalCycles +=
				(Stats.PrefetchStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
				Stats.PrefetchStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));

			TotalCycles +=
				(Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Hit, FCookStats::CallStats::EStatType::Cycles) +
				Stats.PutStats.GetAccumulatedValueAnyThread(FCookStats::CallStats::EHitOrMiss::Miss, FCookStats::CallStats::EStatType::Cycles));
		}
	}

	return TotalCycles * FPlatformTime::GetSecondsPerCycle();
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
		Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Warning"));
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
		})));

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationItem.IsValid())
		{
			NotificationPromise.SetValue(NotificationItem);
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}

	if (DDCProjectSettings->RecommendEveryoneSetupAGlobalS3DDCPath && (Settings->bEnableS3DDC && Settings->GlobalS3DDCPath.Path.IsEmpty()))
	{
		TPromise<TWeakPtr<SNotificationItem>> NotificationPromise;

		FNotificationInfo Info(LOCTEXT("SharedProjectS3DDC", "This project recommends you setup the 'Global Local S3 DDC Path', \nso that all copies of this project use the same local S3 DDC cache."));
		Info.bUseSuccessFailIcons = true;
		Info.Image = FEditorStyle::GetBrush(TEXT("MessageLog.Warning"));
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
		})));

		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		if (NotificationItem.IsValid())
		{
			NotificationPromise.SetValue(NotificationItem);
			NotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}

	return EActiveTimerReturnType::Stop;
}

#undef LOCTEXT_NAMESPACE