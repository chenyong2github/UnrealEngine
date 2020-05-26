// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/**
 * DMX Widget to monitor all the channels of a single DMX Universe
 */

class SDMXInputInfoChannelsView
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXInputInfoChannelsView)
	{}
	SLATE_ARGUMENT(TWeakPtr<class SDMXInputInfoSelecter>, InfoSelecter)
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~End of SWidget interface

	/** Clear the individula channel values in the UI (doesn't clear Protocol channels) */
	void Clear();

	void UniverseSelectionChanged()
	{
		Clear();
		ResetUISequanceID();
	}

	const TArray<uint8>& GetChannelsValues()
	{
		return ChannelsValues;
	}

protected:
	/** Spawns the channel value widgets */
	void CreateChannelValueWidgets();

	/** Poll Input Selector to see if use changed settings */
	void CheckForSelectorChanges();

	/** Copy current values of selected universe to channel widgets */
	void UpdateChannelsValues();

	/** Set the channel widgets with lates values of this universe */
	void UpdateChannelWidgetsValues(const TArray<uint8>& NewValues);

	/** Channel monitor reset all displayed values */
	void ResetUISequanceID() { UISequenceID = 0; }

	/** Container widget for all the channels' values */
	TSharedPtr<class SWrapBox> ChannelValuesBox;
	
	/** Widgets for individual channels, length should be same as number of channels in a universe */
	TArray<TSharedPtr<class SDMXInputInfoChannelValue>> ChannelValueWidgets;

	/** Channels' values for testing purpose */
	TArray<uint8> ChannelsValues;

	TWeakPtr<class SDMXInputInfoSelecter> WeakInfoSelecter;

private:
	/** ID of the sequence on input info widget */
	uint32 UISequenceID;

	uint32 UniverseID;

};