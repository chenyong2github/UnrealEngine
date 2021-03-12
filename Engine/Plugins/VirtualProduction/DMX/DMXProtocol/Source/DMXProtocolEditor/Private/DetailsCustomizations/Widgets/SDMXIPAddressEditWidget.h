// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class SEditableTextBox;
class STextBlock;
class SWidgetSwitcher;


enum class EDMXIPEditWidgetMode
{
	LocalAdapterAddresses,
	EditableTextBox
};

/**
 * Helper widget that draws a local IP address in a combobox.
 */
class SDMXIPAddressEditWidget
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXIPAddressEditWidget)
		: _Mode(EDMXIPEditWidgetMode::LocalAdapterAddresses)
		{}

		SLATE_ARGUMENT(FString, InitialValue)

		SLATE_ARGUMENT(EDMXIPEditWidgetMode, Mode)

		SLATE_EVENT(FSimpleDelegate, OnIPAddressSelected)

	SLATE_END_ARGS()


	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Returns the selected IP Address */
	TSharedPtr<FString> GetSelectedIPAddress() const;

	/** Sets the current widget mode */
	void SetEditMode(EDMXIPEditWidgetMode NewMode);

private:
	/** Sets the current widget mode, but doesn't raise an external event, useful for initialization */
	void SetEditModeInternal(EDMXIPEditWidgetMode NewMode);

	/** Called when the ip address was commited in the editable text block */
	void OnIPAddressCommitted(const FText& InNewText, ETextCommit::Type InTextCommit);

	/** Called when an ip address was selected by any widget */
	void OnIpAddressSelectedInChild();

	/** Widget Switcher to switch between widgets for each mode */
	TSharedPtr<SWidgetSwitcher> WidgetSwitcher;

	/** ComboBox shown when in local adapter mode */
	TSharedPtr<class SDMXLocalAdapterAddressComboBox> LocalAdapterAddressComboBox;

	/** Editable text block for the editable mode */
	TSharedPtr<SEditableTextBox> IPAddressEditableTextBox;

	/** The text in the editable text block, as String */
	FString EditableTextBoxString;

	/** Mode currently in use */
	EDMXIPEditWidgetMode Mode;

	/** Delegate executed when a local IP address was selected */
	FSimpleDelegate OnIPAddressSelected;
};


/** 
 * Helper widget that draws a local adapter addresses in a combobox. 
 */
class SDMXLocalAdapterAddressComboBox
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXLocalAdapterAddressComboBox)
		{}

		SLATE_ARGUMENT(FString, InitialSelection)

		SLATE_EVENT(FSimpleDelegate, OnLocalAdapterAddressSelected)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Selects specified item. Requires the item to be valid. */
	void Select(const TSharedPtr<FString>& LocalAdapterAddress);

	/** Returns the selected IP Address */
	TSharedRef<FString> GetSelectedLocalAdapterAddress() const;

	/** Returns the IP addresses available */
	const TArray<TSharedPtr<FString>>& GetLocalAdapterAddresses() const { return LocalAdapterAddressSource; }

private:
	/** Gets the local IP Addresses Array with the IPs available to the system (NICs, localhost and on Windows the standard 0.0.0.0 route)  */
	void InitializeLocalAdapterAddresses();
	
	/** Generates an entry in the local adapter address combo box */
	TSharedRef<SWidget> GenerateLocalAdapterAddressComboBoxEntry(TSharedPtr<FString> InAddress);

	/** Handles changes in the local adapter address combo box */
	void HandleLocalAdapterAddressSelectionChanged(TSharedPtr<FString> InAddress, ESelectInfo::Type InType);

	/** Array of LocalAdapterAddresses available to the system */
	TArray<TSharedPtr<FString>> LocalAdapterAddressSource;

	/** Text box shown on top of the local adapter address address combo box */
	TSharedPtr<STextBlock> LocalAdapterAddressTextBlock;

	/** The actual combo box that holds the local adapter addresses */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> LocalAdapterAddressComboBox;

	/** Delegate executed when a local adapter address was selected */
	FSimpleDelegate OnLocalAdapterAddressSelected;
};
