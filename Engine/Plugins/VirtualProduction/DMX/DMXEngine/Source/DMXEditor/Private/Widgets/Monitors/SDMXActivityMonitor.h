// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SDMXActivityInUniverse;

class ITableRow;

template <typename OptionType = TSharedPtr<FName>>
class SComboBox;

template <typename ItemType> 
class SListView;

template <typename ValueType> 
class SSpinBox;

class STableViewBase;


/** Constants for the Universe Monitor Widget */
struct FDMXActivityMonitorConstants
{
	/** Name used when the input buffer is the source of the signal monitored */
	static const FName MonitoredSourceInputName;

	/** Text used when the input buffer is the source of the signal monitored */
	static const FText MonitoredSourceInputText;

	/** Name used when the output buffer is the source of the signal monitored */
	static const FName MonitoredSourceOutputName;

	/** Text used when the output buffer is the source of the signal monitored */
	static const FText MonitoredSourceOutputText;
};

/**
 * A Monitor for DMX activity in a range of DMX Universes
 */
class SDMXActivityMonitor
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXActivityMonitor)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

protected:
	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~End of SWidget interface

private:
	/** Initialize values from DMX Protocol Settings */
	void LoadMonitorSettings();

	/** Saves settings of the monitor in config */
	void SaveMonitorSettings() const;

private:
	/** Sets the protocol to be monitored */
	void SetProtocol(FName NewProtocolName);

	/** Returns a buffer view for specified universe ID, creates one if it doesn't exist yet */
	TSharedRef<SDMXActivityInUniverse> GetOrCreateActivityWidget(uint16 UniverseID);

	/** Visualizes an input buffer */
	void VisualizeInputBuffer(FName Protocol, uint16 UniverseID, const TArray<uint8>& Values);

	/** Visualizes output buffer */
	void VisualizeOutputBuffer(FName InProtocol, uint16 InUniverseID, const TArray<uint8>& Values);

private:
	/** Remove all displayed  values on the universes monitor */
	void ClearDisplay();

	/** Adds universes specified in UI to protocol */
	void AddMonitoredUniversesToProtocol();

	/** Handle for the binding to the Input Buffer Update Delegate */
	FDelegateHandle OnUniverseInputBufferUpdatedHandle;

	/** Handle for the binding to the Output Buffer Update Delegate */
	FDelegateHandle OnUniverseOutputBufferUpdatedHandle;

private:
	/** Returns text where monitor signals originate, e.g. Input Buffers or Output Buffers */
	FText GetMonitoredSourceText() const;

	/** Generates an entry in the monitor source combo box */
	TSharedRef<SWidget> GenerateMonitorSourceEntry(TSharedPtr<FName> ProtocolNameToAdd);

	/** Called when a source to be monitored was selected in related combo box */
	void OnMonitoredSourceChanged(TSharedPtr<FName> NewMonitoredSourceName, ESelectInfo::Type SelectInfo);

	/** Source for the combo box to select where the signals monitored originate */
	TArray<TSharedPtr<FName>> MonitoredSourceNamesSource;

	/** Selected name of where signals monitored originate */
	TSharedPtr<FName> MonitoredSourceName;

	/** ComboBox to select where monitored dmx signals originate */
	TSharedPtr<SComboBox<TSharedPtr<FName>>> MonitoredSourceCombobBox;

private:
	/** Called when the clear ui values button was clicked */
	FReply OnClearButtonClicked();

	/** Called when a protocol was selected in the combo box */
	void OnProtocolSelected(FName NewProtocolName);

	/** Called when the user commits the min Universe value */
	void OnMinUniverseIDCommitted(uint32 NewValue, ETextCommit::Type CommitType);

	/** Called when the user commits the max Universe value */
	void OnMaxUniverseIDCommitted(uint32 NewValue, ETextCommit::Type CommitType);

	/** Test if universe ID is in range for the given protocol. */
	bool IsUniverseIDInRange(uint32 InUniverseID) const;

	/** Returns the current protocol name as text */
	FName GetProtocolName() const { return ProtocolName; }

	/** The user-selected protocol name */
	FDMXProtocolName ProtocolName;

	/** The min universe ID to recieve */
	uint32 MinUniverseID;

	/** The max universe ID to recieve */
	uint32 MaxUniverseID;

private:
	/** Called by SListView to generate custom list table row */
	TSharedRef<ITableRow> OnGenerateUniverseRow(TSharedPtr<SDMXActivityInUniverse> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** ListView for universe counts */
	TSharedPtr<SListView<TSharedPtr<SDMXActivityInUniverse>>> UniverseList;

	/** Universe Monitors being displayed */
	TArray<TSharedPtr<SDMXActivityInUniverse>> UniverseListSource;

	/** SpinBox to set the min universe ID */
	TSharedPtr<SSpinBox<uint32>> MinUniverseIDSpinBox;

	/** SpinBox to set the max universe ID */
	TSharedPtr<SSpinBox<uint32>> MaxUniverseIDSpinBox;
};
