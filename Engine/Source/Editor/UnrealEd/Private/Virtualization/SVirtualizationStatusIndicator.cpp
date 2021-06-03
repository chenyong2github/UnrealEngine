// Copyright Epic Games, Inc. All Rights Reserved.

#include "Virtualization/SVirtualizationStatusIndicator.h"

#include "Animation/CurveSequence.h"
#include "Virtualization/VirtualizationManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SVirtualizationStatusIndicator"

namespace UE::Virtualization
{

/** The amount of time (s) that the indication arrows will take to fade in/out */
static constexpr float ArrowFadeTime = 0.5f;

/** 
  * The tool tip that will show when the mouse is hovered over SVirtualizationStatusIndicator. 
  * 
  * Currently this will display the overall amount of data that content virtualization has 
  * transfered to and from the backends.
  */
class SVirtualizationToolTip : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVirtualizationToolTip) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	FText GetToolTipText() const;
};

/** 
 * A widget used to show the status of content virtualization.
 * 
 * A downwards pointing arrow will be displayed when ever a payload is pulled from a backend.
 * An upwards pointing arrow will be displayed when ever a payload is pushed to a backend.
 * This gives the user immediately visual feedback as to when the system is in action.
 * Additional data can be found when hovering the mouse of the widget.
 */
class SVirtualizationStatusIndicator : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SVirtualizationStatusIndicator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	EActiveTimerReturnType UpdateLastDataAccess(double InCurrentTime, float InDeltaTime);

	int64 PayloadsPulled = 0;
	int64 PayloadsPushed = 0;

	FCurveSequence FadePullArrow;
	FCurveSequence FadePushArrow;
};

void SVirtualizationToolTip::Construct(const FArguments& InArgs)
{
	this->ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(this, &SVirtualizationToolTip::GetToolTipText)
		]
	];
}

FText SVirtualizationToolTip::GetToolTipText() const
{
	using namespace UE::Virtualization;
	
	FPayloadActivityInfo Info = FVirtualizationManager::Get().GetPayloadActivityInfo();
	TStringBuilder<1024> Builder;
	
	Builder << TEXT("Total Payload Data Pushed: ") << Info.TotalSizePushed << TEXT(" MB\n");
	Builder << TEXT("Total Payload Data Pulled: ") << Info.TotalSizePulled << TEXT(" MB");
	
	return FText::FromString(Builder.ToString());
}

void SVirtualizationStatusIndicator::Construct(const FArguments& InArgs)
{
	using namespace UE::Virtualization;

	FPayloadActivityInfo Info = FVirtualizationManager::Get().GetPayloadActivityInfo();
	PayloadsPulled = Info.PayloadsPulled;
	PayloadsPushed = Info.PayloadsPushed;
	
	FadePushArrow = FCurveSequence(0.0f, ArrowFadeTime, ECurveEaseFunction::Linear);
	FadePullArrow = FCurveSequence(0.0f, ArrowFadeTime, ECurveEaseFunction::Linear);

	const FText MirageLabel = FText::FromString(TEXT("Mirage"));

	this->ChildSlot
	[
		SNew(SHorizontalBox)
		.ToolTip(SNew(SToolTip)[SNew(SVirtualizationToolTip)])

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
				.ColorAndOpacity_Lambda([this] { return FLinearColor::Green.CopyWithNewOpacity(FadePushArrow.GetLerp()); })
			]

			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			.Padding(4, 4, 0, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.ArrowDown"))
				.ColorAndOpacity_Lambda([this] { return FLinearColor::Green.CopyWithNewOpacity(FadePullArrow.GetLerp()); })
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0, 0, 10, 0)
		[
			SNew(STextBlock)
			.Text(MirageLabel)
		]
	];

	RegisterActiveTimer(ArrowFadeTime, FWidgetActiveTimerDelegate::CreateSP(this, &SVirtualizationStatusIndicator::UpdateLastDataAccess));
}

EActiveTimerReturnType SVirtualizationStatusIndicator::UpdateLastDataAccess(double InCurrentTime, float InDeltaTime)
{
	using namespace UE::Virtualization;

	FPayloadActivityInfo Info = FVirtualizationManager::Get().GetPayloadActivityInfo();
	
	// Don't change the fade if it is currently playing, the tick duration of this method is not that accurate
	// and we don't want to cut out the previous fade too soon or the user might not be able to notice it.

	if (!FadePullArrow.IsPlaying())
	{
		FadePullArrow.PlayRelative(this->AsShared(), PayloadsPulled != Info.PayloadsPulled);
		PayloadsPulled = Info.PayloadsPulled;
	}

	if (!FadePushArrow.IsPlaying())
	{
		FadePushArrow.PlayRelative(this->AsShared(), PayloadsPushed != Info.PayloadsPushed);
		PayloadsPushed = Info.PayloadsPushed;
	}

	return EActiveTimerReturnType::Continue;
}

TSharedPtr<SWidget> GetVirtualizationStatusIndicator()
{
	if (FVirtualizationManager::Get().IsEnabled())
	{
		return SNew(SVirtualizationStatusIndicator);
	}
	else
	{
		return nullptr;
	}
}

} // namespace UE::Virtualization

#undef LOCTEXT_NAMESPACE
