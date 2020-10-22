// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAggregatorStatus.h"

#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/StatsAggregator.h"

#define LOCTEXT_NAMESPACE "SAggregatorStatus"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAggregatorStatus::Construct(const FArguments& InArgs, TSharedRef<IStatsAggregator> InAggregator)
{
	Aggregator = InAggregator;

	ChildSlot
	[
		SNew(SBox)
		.Visibility(this, &SAggregatorStatus::GetContentVisibility)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
			.BorderBackgroundColor(this, &SAggregatorStatus::GetBackgroundColorAndOpacity)
			.Padding(FMargin(16.0f, 8.0f, 16.0f, 8.0f))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.Text(this, &SAggregatorStatus::GetText)
					.ToolTipText(this, &SAggregatorStatus::GetTooltipText)
					.ColorAndOpacity(this, &SAggregatorStatus::GetTextColorAndOpacity)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.MinDesiredWidth(16.0f)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.Text(this, &SAggregatorStatus::GetAnimatedText)
					.ToolTipText(this, &SAggregatorStatus::GetTooltipText)
					.ColorAndOpacity(this, &SAggregatorStatus::GetTextColorAndOpacity)
				]
			]
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SAggregatorStatus::GetContentVisibility() const
{
	return Aggregator->IsRunning() ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SAggregatorStatus::GetBackgroundColorAndOpacity() const
{
	const float Opacity = ComputeOpacity();
	return FSlateColor(FLinearColor(0.2f, 0.2f, 0.2f, Opacity));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SAggregatorStatus::GetTextColorAndOpacity() const
{
	const float Opacity = ComputeOpacity();
	return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float SAggregatorStatus::ComputeOpacity() const
{
	const double TotalDuration = Aggregator->GetAllOperationsDuration();
	constexpr double FadeInStartTime = 0.1; // [second]
	constexpr double FadeInEndTime = 3.0; // [second]
	constexpr double FadeInDuration = FadeInEndTime - FadeInStartTime;
	const float Opacity = static_cast<float>(FMath::Clamp(TotalDuration - FadeInStartTime, 0.0, FadeInDuration) / FadeInDuration);
	return Opacity;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAggregatorStatus::GetText() const
{
	return LOCTEXT("DefaultText", "Computing");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAggregatorStatus::GetAnimatedText() const
{
	const double TotalDuration = Aggregator->GetAllOperationsDuration();
	const TCHAR* Anim[] = { TEXT(""), TEXT("."), TEXT(".."), TEXT("..."), };
	int32 AnimIndex = static_cast<int32>(TotalDuration / 0.2) % UE_ARRAY_COUNT(Anim);
	return FText::FromString(FString(Anim[AnimIndex]));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAggregatorStatus::GetTooltipText() const
{
	const double TotalDuration = Aggregator->GetAllOperationsDuration();
	const uint32 OperationCount = Aggregator->GetOperationCount();
	if (OperationCount == 1)
	{
		return FText::Format(LOCTEXT("DefaultTooltip_Fmt2", "Computing aggregated stats...\nElapsed Time: {0}"),
			FText::FromString(TimeUtils::FormatTime(TotalDuration, TimeUtils::Second)));
	}
	else
	{
		const double Duration = Aggregator->GetCurrentOperationDuration();
		return FText::Format(LOCTEXT("DefaultTooltip_Fmt3", "Computing aggregated stats...\nElapsed Time: {0}\nElapsed Time (op {1}): {2}"),
			FText::FromString(TimeUtils::FormatTime(TotalDuration, TimeUtils::Second)),
			FText::AsNumber(OperationCount),
			FText::FromString(TimeUtils::FormatTime(Duration, TimeUtils::Second)));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
