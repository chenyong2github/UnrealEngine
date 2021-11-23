// Copyright Epic Games, Inc. All Rights Reserved.

#include "SVirtualAssetsStatistics.h"
#include "VirtualizationManager.h"
#include "Math/UnitConversion.h"
#include "Math/BasicMathExpressionEvaluator.h"
#include "Misc/ExpressionParser.h"
#include "Styling/StyleColors.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Internationalization/FastDecimalFormat.h"

#define LOCTEXT_NAMESPACE "VirtualAssets"

extern FString SingleDecimalFormat(double Value);

using namespace UE::Virtualization;

void SVirtualAssetsStatisticsDialog::Construct(const FArguments& InArgs)
{
	const float RowMargin = 0.0f;
	const float TitleMargin = 10.0f;
	const float ColumnMargin = 10.0f;
	const FSlateColor TitleColour = FStyleColors::AccentWhite;
	const FSlateFontInfo TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);

	this->ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0, 20, 0, 0)
		.Expose(GridSlot)
		[
			GetGridPanel()
		]
	];

	RegisterActiveTimer(0.5f, FWidgetActiveTimerDelegate::CreateSP(this, &SVirtualAssetsStatisticsDialog::UpdateGridPanels));
}

EActiveTimerReturnType SVirtualAssetsStatisticsDialog::UpdateGridPanels(double InCurrentTime, float InDeltaTime)
{
	(*GridSlot)
	[
		GetGridPanel()
	];

	SlatePrepass(GetPrepassLayoutScaleMultiplier());

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

		auto DisplayPayloadActivityInfo = [&](const FString& DebugName, const FString& ConfigName, const FPayloadActivityInfo& PayloadAcitvityInfo)
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
				.Text_Lambda([PayloadAcitvityInfo] { return FText::FromString(FString::Printf(TEXT("%u"), PayloadAcitvityInfo.Pull.PayloadCount)); })
				];

			Panel->AddSlot(2, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo, BytesToMegaBytes] { return FText::FromString(SingleDecimalFormat((double)PayloadAcitvityInfo.Pull.TotalBytes * BytesToMegaBytes)); })
				];

			Panel->AddSlot(3, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo] { return FText::FromString(SingleDecimalFormat((double)PayloadAcitvityInfo.Pull.CyclesSpent * FPlatformTime::GetSecondsPerCycle())); })
				];

			Panel->AddSlot(4, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo] { return FText::FromString(FString::Printf(TEXT("%u"), PayloadAcitvityInfo.Push.PayloadCount)); })
				];

			Panel->AddSlot(5, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo, BytesToMegaBytes] { return FText::FromString(SingleDecimalFormat((double)PayloadAcitvityInfo.Push.TotalBytes * BytesToMegaBytes)); })
				];

			Panel->AddSlot(6, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo] { return FText::FromString(SingleDecimalFormat((double)PayloadAcitvityInfo.Push.CyclesSpent * FPlatformTime::GetSecondsPerCycle())); })
				];

			Panel->AddSlot(7, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo] { return FText::FromString(FString::Printf(TEXT("%u"), PayloadAcitvityInfo.Cache.PayloadCount)); })
				];

			Panel->AddSlot(8, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo, BytesToMegaBytes] { return FText::FromString(SingleDecimalFormat((double)PayloadAcitvityInfo.Cache.TotalBytes * BytesToMegaBytes)); })
				];

			Panel->AddSlot(9, Row)
				[
					SNew(STextBlock)
					.Margin(FMargin(ColumnMargin, RowMargin))
				.ColorAndOpacity(Color)
				.Font(Font)
				.Justification(ETextJustify::Center)
				.Text_Lambda([PayloadAcitvityInfo] { return FText::FromString(SingleDecimalFormat((double)PayloadAcitvityInfo.Cache.CyclesSpent * FPlatformTime::GetSecondsPerCycle())); })
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
