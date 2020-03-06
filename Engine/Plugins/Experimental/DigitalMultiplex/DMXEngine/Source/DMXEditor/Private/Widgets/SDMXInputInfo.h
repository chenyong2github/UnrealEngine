// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SWrapBox;
class SDMXInputInfoChannelValue;
class SDMXInputInfoSelecter;

/** DMX Input inspector widget */
class SDMXInputInfo
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXInputInfo)
	{}
	SLATE_ARGUMENT(TWeakPtr<SDMXInputInfoSelecter>, InfoSelecter)
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~End of SWidget interface

	const TArray<uint8>& GetChannelsValues() const { return ChannelsValues; }

	void ResetUISequanceID() { UISequenceID = 0; }
	
protected:
	/** Container widget for all the channels' values */
	TSharedPtr<SWrapBox> ChannelValuesBox;

	/** Channels' values for testing purpose */
	TArray<uint8> ChannelsValues;

	TArray<TSharedPtr<SDMXInputInfoChannelValue>> ChannelValueWidgets;

	TWeakPtr<SDMXInputInfoSelecter> WeakInfoSelecter;

protected:
	/** Spawns the channel value widgets */
	void CreateChannelValueWidgets();

	/** channel values */
	void UpdateChannelsValues();

	void UpdateChannelWidgetsValues(const TArray<uint8>& NewValues);

private:
	/** ID of the sequence on input info widget */
	uint32 UISequenceID;
}; 