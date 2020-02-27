// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputInfoChannelValue.h"

#include "Interfaces/IDMXProtocol.h"
#include "DMXEditorLog.h"
#include "DMXProtocolConstants.h"
#include "DMXEditorStyle.h"

#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Images/SImage.h"
#include "Brushes/SlateColorBrush.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/Font.h"

#define LOCTEXT_NAMESPACE "SDMXInputInfo"

const float SDMXInputInfoChannelValue::NewValueChangedAnimDuration = 0.8f;

const FLinearColor SDMXInputInfoChannelValue::IDColor = FLinearColor(1,1,1, 0.6f);
const FLinearColor SDMXInputInfoChannelValue::ValueColor = FLinearColor(1,1,1, 0.9f);

void SDMXInputInfoChannelValue::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::SelfHitTestInvisible);
	SetCanTick(false);

	BoundID = InArgs._ID;
	BoundValue = InArgs._Value;
	NewValueFreshness = 0.0f;

	const float PaddingInfo = 3.0f;

	ChildSlot
	[
		// root
		SNew(SOverlay)

		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			// Background color image
			SAssignNew(BarColorBorder, SImage)
			.Image(FDMXEditorStyle::Get().GetBrush(TEXT("DMXEditor.WhiteBrush")))
			.ColorAndOpacity(this, &SDMXInputInfoChannelValue::GetBackgroundColor)
		]

		// Info
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.Padding(PaddingInfo)
		[
			SNew(SVerticalBox)

			// ID Label
			+ SVerticalBox::Slot()
			.FillHeight(0.5f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SDMXInputInfoChannelValue::GetIDLabel)
				.ColorAndOpacity(FSlateColor(IDColor))
				.MinDesiredWidth(23.0f)
				.Justification(ETextJustify::Center)
				.Font(FDMXEditorStyle::Get().GetFontStyle("DMXEditor.Font.InputChannelID"))
			]

			// Separator
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			[
				SNew(SSeparator)
				.Orientation(Orient_Horizontal)
			]

			// Value Label
			+ SVerticalBox::Slot()
			.FillHeight(0.5f)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SDMXInputInfoChannelValue::GetValueLabel)
				.ColorAndOpacity(FSlateColor(ValueColor))
				.MinDesiredWidth(23.0f)
				.Justification(ETextJustify::Center)
				.Font(FDMXEditorStyle::Get().GetFontStyle("DMXEditor.Font.InputChannelValue"))
			]
		]
	];
}

void SDMXInputInfoChannelValue::SetID(const TAttribute<uint32>& NewID)
{
	BoundID = NewID;
}

void SDMXInputInfoChannelValue::SetValue(const TAttribute<uint8>& NewValue)
{
	// is NewValue a different value from current one?
	if (NewValue.Get() != BoundValue.Get())
	{
		// Activate timer to animate value bar color
		if (!AnimationTimerHandle.IsValid())
		{
			AnimationTimerHandle = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this, &SDMXInputInfoChannelValue::UpdateValueChangedAnim));
		}
		// restart value change animation
		NewValueFreshness = 1.0f;
	}
	BoundValue = NewValue;
}

EActiveTimerReturnType SDMXInputInfoChannelValue::UpdateValueChangedAnim(double InCurrentTime, float InDeltaTime)
{
	NewValueFreshness = FMath::Max(NewValueFreshness - InDeltaTime / NewValueChangedAnimDuration, 0.0f);
	
	// disable timer when the value bar color animation ends
	if (NewValueFreshness <= 0.0f)
	{
		TSharedPtr<FActiveTimerHandle> PinnedTimerHandle = AnimationTimerHandle.Pin();
		if (PinnedTimerHandle.IsValid())
		{
			UnRegisterActiveTimer(PinnedTimerHandle.ToSharedRef());
		}
	}
	return EActiveTimerReturnType::Continue;
}

FText SDMXInputInfoChannelValue::GetIDLabel() const
{
	return FText::AsNumber(BoundID.Get());
}

FText SDMXInputInfoChannelValue::GetValueLabel() const
{
	return FText::AsNumber(BoundValue.Get());
}

FSlateColor SDMXInputInfoChannelValue::GetBackgroundColor() const
{
	const float CurrentPercent = static_cast<float>(BoundValue.Get()) / DMX_MAX_CHANNEL_VALUE;

	// totally transparent when 0
	if (CurrentPercent <= 0.0f)
	{
		return FLinearColor(0, 0, 0, 0);
	}

	// Intensities to be animated when a new value is set and then multiplied by the background color
	static const float NormalIntensity = 0.3f;
	static const float FreshValueIntensity = 0.7f;
	// lerp intensity depending on NewValueFreshness^2 to make it pop for a while when it has just been updated
	const float ValueFreshnessIntensity = FMath::Lerp(NormalIntensity, FreshValueIntensity, NewValueFreshness * NewValueFreshness);

	// color variations for low and high channel values
	static const FVector LowValueColor = FVector(0, 0.045f, 0.15f);
	static const FVector HighValueColor = FVector(0, 0.3f, 1.0f);
	const FVector ColorFromChannelValue = FMath::Lerp(LowValueColor, HighValueColor, CurrentPercent);

	// returning a FVector, a new FSlateColor will be created from it with (RGB = vector, Alpha = 1.0)
	FVector Result = ColorFromChannelValue * ValueFreshnessIntensity;
	return FSlateColor(FLinearColor(Result.X, Result.Y, Result.Z));
}

#undef LOCTEXT_NAMESPACE
