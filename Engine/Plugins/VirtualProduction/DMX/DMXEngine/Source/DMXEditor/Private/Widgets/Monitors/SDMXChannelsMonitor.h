// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolTypes.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

template<typename NumericType>
class SSpinBox;

/**
 * DMX Widget to monitor all the channels in a DMX Universe
 */
class SDMXChannelsMonitor
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXChannelsMonitor)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

protected:
	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~End of SWidget interface

private:
	/** Creates the channel value widgets */
	void CreateChannelValueWidgets();

	/** Initialize values from DMX Protocol Settings */
	void LoadMonitorSettings();

	/** Saves settings of the monitor in config */
	void SaveMonitorSettings() const;

private:
	/** Copy current values of selected universe to channel widgets */
	void UpdateBuffer();

	/** Set the channel widgets with lates values of this universe */
	void UpdateChannelValueWidgets();

	/** Channel monitor reset all displayed values */
	void ResetUISequenceID() { UISequenceID = 0; }

	/** Called when the protocol changed */
	void OnProtocolChanged(FName SelectedProtocol);

	/** Called when the universe ID was committed */
	void OnUniverseIDValueCommitted(uint32 NewValue, ETextCommit::Type CommitType);
	
	/** Called when the clear bButton was clicked */
	FReply OnClearButtonClicked();

	/** Returns the name of the user-selected DMX protocol */
	FName GetProtocolName() const { return ProtocolName; }

private:
	/** Container widget for all the channels' values */
	TSharedPtr<class SWrapBox> ChannelValuesBox;
	
	/** Widgets for individual channels, length should be same as number of channels in a universe */
	TArray<TSharedPtr<class SDMXChannel>> ChannelValueWidgets;

	/** Universe field widget */
	TSharedPtr<SSpinBox<uint32>> UniverseIDSpinBox;

	/** Channel values for testing purpose */
	TArray<uint8> Buffer;

	/** The user-selected protocol */
	FDMXProtocolName ProtocolName;

private:
	/** Universe ID value computed using Net, Subnet and Universe values */
	uint16 UniverseID = 1;
	
	/** ID of the sequence on input info widget */
	uint32 UISequenceID;
};
