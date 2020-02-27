// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SImage;
class FActiveTimerHandle;

/** DMX Input Channel value representation widget */
class SDMXInputInfoChannelValue
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXInputInfoChannelValue)
		: _ID(0)
		, _Value(0)
		{}

	/** The channel ID this widget represents */
	SLATE_ATTRIBUTE(uint32, ID)

	/** The current value from the channel */
	SLATE_ATTRIBUTE(uint8, Value)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

public:
	/** Sets the channel ID this widget represents */
	void SetID(const TAttribute<uint32>& NewID);
	/** Gets the channel ID this widget represents */
	uint32 GetID() const
	{
		return BoundID.Get();
	}

	/** Sets the current value from the channel */
	void SetValue(const TAttribute<uint8>& NewValue);
	/** Gets the current value from the channel */
	uint8 GetValue() const
	{
		return BoundValue.Get();
	}

	/**
	 * Updates the variable that controls the color animation progress for the Value Bar.
	 * This is called by a timer.
	 */
	EActiveTimerReturnType UpdateValueChangedAnim(double InCurrentTime, float InDeltaTime);

private:
	/** The channel ID this widget represents */
	TAttribute<uint32> BoundID;
	/** The current value from the channel */
	TAttribute<uint8> BoundValue;

	/** The ProgressBar widget to display the channel value graphically */
	TSharedPtr<SImage> BarColorBorder;

	/**
	 * Used to animate the color when the value changes.
	 * 0..1 range: 1 = value has just changed, 0 = standard color
	 */
	float NewValueFreshness;
	/** How long it takes to become standard color again after a new value is set */
	static const float NewValueChangedAnimDuration;
	/** Used to stop the animation timer once the animation is completed */
	TWeakPtr<FActiveTimerHandle> AnimationTimerHandle;

	// ~ VISUAL STYLE CONSTANTS

	/** Color of the ID label */
	static const FLinearColor IDColor;

	/** Color of the Value label */
	static const FLinearColor ValueColor;

private:
	/** Returns the channel ID in Text form to display it in the UI */
	FText GetIDLabel() const;
	/** Returns the channel value in Text form to display it in the UI */
	FText GetValueLabel() const;

	/** Returns the fill color for the ValueBar */
	FSlateColor GetBackgroundColor() const;
};
