// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVirtualAssetsStatistics.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Internationalization/FastDecimalFormat.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Math/UnitConversion.h"
#include "Misc/ExpressionParser.h"
#include "Styling/StyleColors.h"
#include "VirtualizationManager.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "VirtualAssets"

extern FString SingleDecimalFormat(double Value);

using namespace UE::Virtualization;

SVirtualAssetsStatisticsDialog::SVirtualAssetsStatisticsDialog()
{
	// Register our VA notification delegate with the event
	IVirtualizationSystem& System = IVirtualizationSystem::Get();
	System.GetNotificationEvent().AddRaw(this, &SVirtualAssetsStatisticsDialog::OnNotificationEvent);

}

SVirtualAssetsStatisticsDialog::~SVirtualAssetsStatisticsDialog()
{
	// Unregister our VA notification delegate with the event
	IVirtualizationSystem& System = IVirtualizationSystem::Get();
	System.GetNotificationEvent().RemoveAll(this);
}

void SVirtualAssetsStatisticsDialog::OnNotificationEvent(IVirtualizationSystem::ENotification Notification, const FIoHash& PayloadId)
{
	FScopeLock SocpeLock(&NotificationCS);
	
	switch (Notification)
	{	
		case IVirtualizationSystem::ENotification::PullBegunNotification:
		{
			IsPulling = true;
			NumPullRequests++;

			break;
		}

		case IVirtualizationSystem::ENotification::PullEndedNotification:
		{	
			if (IsPulling == true)
			{
				NumPullRequests--;
				IsPulling = NumPullRequests!=0;
			}
			
			break;
		}

		case IVirtualizationSystem::ENotification::PullFailedNotification:
		{
			FNotificationInfo* Info = new FNotificationInfo(LOCTEXT("PayloadSyncFail", "Failed To Sync Asset Payload"));

			Info->bUseSuccessFailIcons = true;
			Info->bFireAndForget = true;
			Info->FadeOutDuration = 1.0f;
			Info->ExpireDuration = 4.0f;

			FSlateNotificationManager::Get().QueueNotification(Info);

			break;
		}

		default:
		break;
	}
}

void SVirtualAssetsStatisticsDialog::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 20, 0, 0)
		.Expose(GridSlot)
		[
			SAssignNew(ScrollBox, SScrollBox)
			.Orientation(EOrientation::Orient_Horizontal)
			.ScrollBarAlwaysVisible(false)

			+ SScrollBox::Slot()
			[
				GetGridPanel()
			]	
		]
	];

	RegisterActiveTimer(0.25f, FWidgetActiveTimerDelegate::CreateSP(this, &SVirtualAssetsStatisticsDialog::UpdateGridPanels));
}

EActiveTimerReturnType SVirtualAssetsStatisticsDialog::UpdateGridPanels(double InCurrentTime, float InDeltaTime)
{
	ScrollBox->ClearChildren();
	ScrollBox->AddSlot()
		[
			GetGridPanel()
		];
		
	SlatePrepass(GetPrepassLayoutScaleMultiplier());

	const float PullNotifactionTimeLimit=1.0f;

	// Only show the pull notification if we have been pulling for more than a second..
	if (NumPullRequests != 0)
	{
		PullNotificationTimer += InDeltaTime;
	}
	else
	{
		PullNotificationTimer = 0.0f;
	}

	if ( PullNotificationTimer>PullNotifactionTimeLimit && PullRequestNotificationItem.IsValid()==false )
	{
		// No existing notification or the existing one has finished
		TPromise<TWeakPtr<SNotificationItem>> NotificationPromise;

		FNotificationInfo Info(LOCTEXT("PayloadSyncInProgress", "Syncing Asset Payloads"));
		Info.bFireAndForget = false;
		Info.FadeOutDuration = 0.5f;
		Info.ExpireDuration = 0.0f;

		PullRequestNotificationItem = FSlateNotificationManager::Get().AddNotification(Info);

		if (PullRequestNotificationItem.IsValid())
		{
			NotificationPromise.SetValue(PullRequestNotificationItem);
			PullRequestNotificationItem->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}
	
	if ( NumPullRequests==0 && PullRequestNotificationItem.IsValid()==true )
	{
		PullRequestNotificationItem->SetCompletionState(SNotificationItem::CS_Success);
		PullRequestNotificationItem->ExpireAndFadeout();
		PullRequestNotificationItem.Reset();
	}

	return EActiveTimerReturnType::Continue;
}

TSharedRef<SWidget> SVirtualAssetsStatisticsDialog::GetGridPanel()
{
	IVirtualizationSystem& System = IVirtualizationSystem::Get();

	TSharedRef<SGridPanel> Panel = SNew(SGridPanel);

	const float RowMargin = 0.0f;
	const float TitleMargin = 10.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColor = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
	const double BytesToMegaBytes = 1.0 / (1024.0 * 1024.0);

	if (System.IsEnabled() == false)
	{
		Panel->AddSlot(0,0)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Disabled", "Virtual Assets Are Disabled For This Project"))
			];
	}
	else
	{
		int32 Row = 0;

		Panel->AddSlot(2, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Read", "Read"))
			];

		Panel->AddSlot(5, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Write", "Write"))
			];

		Panel->AddSlot(8, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Cache", "Cache"))
			];

		Row++;

		Panel->AddSlot(0, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Left)
				.Text(LOCTEXT("Backend", "Backend"))
			];

		Panel->AddSlot(1, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Count", "Count"))
			];

		Panel->AddSlot(2, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Time", "Time (Sec)"))
			];

		Panel->AddSlot(3, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Size", "Size (MB)"))
			];

		Panel->AddSlot(4, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Count", "Count"))
			];

		Panel->AddSlot(5, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Time", "Time (Sec)"))
			];

		Panel->AddSlot(6, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Size", "Size (MB)"))
			];

		Panel->AddSlot(7, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Count", "Count"))
			];

		Panel->AddSlot(8, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Time", "Time (Sec)"))
			];

		Panel->AddSlot(9, Row)
			[
				SNew(STextBlock)
				.Margin(FMargin(ColumnMargin, RowMargin, 0.0f, TitleMargin))
				.ColorAndOpacity(TitleColor)
				.Font(TitleFont)
				.Justification(ETextJustify::Center)
				.Text(LOCTEXT("Size", "Size (MB)"))
			];

		Row++;

		FPayloadActivityInfo AccumulatedPayloadAcitvityInfo = System.GetAccumualtedPayloadActivityInfo();

		FSlateColor Color = FStyleColors::Foreground;
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);

		auto DisplayPayloadActivityInfo = [&](const FString& DebugName, const FString& ConfigName, const FPayloadActivityInfo& PayloadActivityInfo)
		{
			Panel->AddSlot(0, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Left)
					.Text(FText::FromString(DebugName))
				];

			Panel->AddSlot(1, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo] { return FText::FromString(FString::Printf(TEXT("%u"), PayloadActivityInfo.Pull.PayloadCount)); })
				];

			Panel->AddSlot(2, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo] { return FText::FromString(SingleDecimalFormat((double)PayloadActivityInfo.Pull.CyclesSpent * FPlatformTime::GetSecondsPerCycle())); })
				];

			Panel->AddSlot(3, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo, BytesToMegaBytes] { return FText::FromString(SingleDecimalFormat((double)PayloadActivityInfo.Pull.TotalBytes * BytesToMegaBytes)); })
				];

			Panel->AddSlot(4, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo] { return FText::FromString(FString::Printf(TEXT("%u"), PayloadActivityInfo.Push.PayloadCount)); })
				];

			Panel->AddSlot(5, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo] { return FText::FromString(SingleDecimalFormat((double)PayloadActivityInfo.Push.CyclesSpent * FPlatformTime::GetSecondsPerCycle())); })
				];

			Panel->AddSlot(6, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo, BytesToMegaBytes] { return FText::FromString(SingleDecimalFormat((double)PayloadActivityInfo.Push.TotalBytes * BytesToMegaBytes)); })
				];

			Panel->AddSlot(7, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo] { return FText::FromString(FString::Printf(TEXT("%u"), PayloadActivityInfo.Cache.PayloadCount)); })
				];

			Panel->AddSlot(8, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo] { return FText::FromString(SingleDecimalFormat((double)PayloadActivityInfo.Cache.CyclesSpent * FPlatformTime::GetSecondsPerCycle())); })
				];

			Panel->AddSlot(9, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
					.ColorAndOpacity(Color)
					.Font(Font)
					.Justification(ETextJustify::Center)
					.Text_Lambda([PayloadActivityInfo, BytesToMegaBytes] { return FText::FromString(SingleDecimalFormat((double)PayloadActivityInfo.Cache.TotalBytes * BytesToMegaBytes)); })
				];

			Row++;
		};

		System.GetPayloadActivityInfo(DisplayPayloadActivityInfo);

		Color = TitleColor;
		Font = TitleFont;

		DisplayPayloadActivityInfo(FString("Total"), FString("Total"), AccumulatedPayloadAcitvityInfo);
	}

	return Panel;
}

#undef LOCTEXT_NAMESPACE
