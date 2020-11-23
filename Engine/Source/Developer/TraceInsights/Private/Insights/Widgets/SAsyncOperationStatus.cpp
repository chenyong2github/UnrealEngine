// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAsyncOperationStatus.h"

#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#include "Insights/Common/InsightsAsyncWorkUtils.h"
#include "Insights/Common/TimeUtils.h"

#define LOCTEXT_NAMESPACE "SAsyncOperationStatus"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

void SAsyncOperationStatus::Construct(const FArguments& InArgs, TSharedRef<IAsyncOperationStatusProvider> InStatusProvider)
{
	StatusProvider = InStatusProvider;

	ChildSlot
	[
		SNew(SBox)
		.Visibility(this, &SAsyncOperationStatus::GetContentVisibility)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NotificationList.ItemBackground"))
			.BorderBackgroundColor(this, &SAsyncOperationStatus::GetBackgroundColorAndOpacity)
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
					.Text(this, &SAsyncOperationStatus::GetText)
					.ToolTipText(this, &SAsyncOperationStatus::GetTooltipText)
					.ColorAndOpacity(this, &SAsyncOperationStatus::GetTextColorAndOpacity)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.MinDesiredWidth(16.0f)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
					.Text(this, &SAsyncOperationStatus::GetAnimatedText)
					.ToolTipText(this, &SAsyncOperationStatus::GetTooltipText)
					.ColorAndOpacity(this, &SAsyncOperationStatus::GetTextColorAndOpacity)
				]
			]
		]
	];
}

////////////////////////////////////////////////////////////////////////////////////////////////////

EVisibility SAsyncOperationStatus::GetContentVisibility() const
{
	return StatusProvider->IsRunning() ? EVisibility::Visible : EVisibility::Collapsed;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SAsyncOperationStatus::GetBackgroundColorAndOpacity() const
{
	const float Opacity = ComputeOpacity();
	return FSlateColor(FLinearColor(0.2f, 0.2f, 0.2f, Opacity));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FSlateColor SAsyncOperationStatus::GetTextColorAndOpacity() const
{
	const float Opacity = ComputeOpacity();
	return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Opacity));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

float SAsyncOperationStatus::ComputeOpacity() const
{
	const double TotalDuration = StatusProvider->GetAllOperationsDuration();
	constexpr double FadeInStartTime = 0.1; // [second]
	constexpr double FadeInEndTime = 3.0; // [second]
	constexpr double FadeInDuration = FadeInEndTime - FadeInStartTime;
	const float Opacity = static_cast<float>(FMath::Clamp(TotalDuration - FadeInStartTime, 0.0, FadeInDuration) / FadeInDuration);
	return Opacity;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAsyncOperationStatus::GetText() const
{
	return LOCTEXT("DefaultText", "Computing");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAsyncOperationStatus::GetAnimatedText() const
{
	const double TotalDuration = StatusProvider->GetAllOperationsDuration();
	const TCHAR* Anim[] = { TEXT(""), TEXT("."), TEXT(".."), TEXT("..."), };
	int32 AnimIndex = static_cast<int32>(TotalDuration / 0.2) % UE_ARRAY_COUNT(Anim);
	return FText::FromString(FString(Anim[AnimIndex]));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText SAsyncOperationStatus::GetTooltipText() const
{
	const double TotalDuration = StatusProvider->GetAllOperationsDuration();
	const uint32 OperationCount = StatusProvider->GetOperationCount();
	FText CurrentOpName = StatusProvider->GetCurrentOperationName();

	if (OperationCount == 1)
	{
		return FText::Format(LOCTEXT("DefaultTooltip_Fmt2", "{0}...\nElapsed Time: {1}"),
			CurrentOpName,
			FText::FromString(TimeUtils::FormatTime(TotalDuration, TimeUtils::Second)));
	}
	else
	{
		const double Duration = StatusProvider->GetCurrentOperationDuration();
		return FText::Format(LOCTEXT("DefaultTooltip_Fmt3", "{0}...\nElapsed Time: {1}\nElapsed Time (op {2}): {3}"),
			CurrentOpName,
			FText::FromString(TimeUtils::FormatTime(TotalDuration, TimeUtils::Second)),
			FText::AsNumber(OperationCount),
			FText::FromString(TimeUtils::FormatTime(Duration, TimeUtils::Second)));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
