// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

struct FUniverseCount;

/** DMX Input Universe Packet Counts representation widget */
class SDMXInputInfoUniverseCounts
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXInputInfoUniverseCounts)
		: _ID(0)
		, _Value(TSharedPtr<FUniverseCount>())
	{
	}

	/** The universe ID this widget represents */
	SLATE_ATTRIBUTE(uint32, ID)

	/** The count of packets received for this universe */
	SLATE_ATTRIBUTE(TSharedPtr<FUniverseCount>, Value)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

public:
	/** Sets the universe ID this widget represents */
	void SetID(const TAttribute<uint32>& NewID);

	/** Gets the universe ID this widget represents */
	uint32 GetID() const
	{
		return BoundID.Get();
	}

	/** Sets the current channel values */
	void SetValue(const TAttribute<TSharedPtr<FUniverseCount>>& NewValue);

	/** Gets current channel values*/
	TSharedPtr<FUniverseCount> GetValue() const
	{
		return BoundValue.Get();
	}

	/**
	 * Updates the variable that controls the color animation progress for the Value Bar.
	 * This is called by a timer.
	 */
	EActiveTimerReturnType UpdateValueChangedAnim(double InCurrentTime, float InDeltaTime);

protected:
	/** Called for new universes or changes to channel values of exising universes */
	void UpdateChannelsView();

	/** Generates a widget for the SList for a new universe */
	TSharedRef<class ITableRow> GenerateRow(TSharedPtr<class SDMXInputInfoUniverseChannelView> InChannelView, const TSharedRef<STableViewBase>& OwnerTable);

private:
	/** The universe ID this widget represents */
	TAttribute<uint32> BoundID;

	/** The number of packets received for this universe */
	TAttribute<TSharedPtr<FUniverseCount>> BoundValue;

	/** The ProgressBar widget to display the channel value graphically */
	TSharedPtr<class SImage> BarColorBorder;

	/** List view widget that displays current non-zero values for all universes */
	TSharedPtr<SListView<TSharedPtr<class SDMXInputInfoUniverseChannelView>>> ChannelsView;

	/** Widgets for view of all non-zero channel values of each universe */
	TArray<TSharedPtr<class SDMXInputInfoUniverseChannelView>> ChannelValuesViews;

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

	/** Returns the universe ID in Text form to display it in the UI */
	FText GetIDLabel() const;
	///** Returns the universe packet count value in Text form to display it in the UI */
	//FText GetValueLabel() const;

	/** Returns the fill color for the ValueBar */
	FSlateColor GetBackgroundColor() const;
};