// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Widgets/SCompoundWidget.h"

template<typename OptionType>
class SComboBox;


DECLARE_DELEGATE_OneParam(FOnAttributeSelectionChanged, const FName& /** AttributeName */)

class SDMXPixelMappingAttributeNamesComboBox
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingAttributeNamesComboBox)
	{}

		/** Event raised when the selction changed */
		SLATE_EVENT(FOnAttributeSelectionChanged, OnSelectionChanged)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs, const TArray<FName>& InOptions);

	/** Sets the selection. Fails silently if the Name is not contained in options provided during construction */
	void SetSelection(const FName& Selection);

	/** Sets the combo box to display 'Multiple Values'. Note, this is reset when the combo box selects a value. */
	void SetHasMultipleValues();

private:
	/*
	 * Make a widget for the given option
	 *
	 * @param InOption   An option from which to construct the widget
	 */
	TSharedRef<SWidget> OnGenerateComboBoxEntry(TSharedPtr<FName> AttributeName) const;

	/*
	 * Handle current item selection change
	 *
	 * @param NewValue   A value to set as current item
	 */
	void HandleSelectionChanged(TSharedPtr<FName> NewValue, ESelectInfo::Type);

private:
	/** The actual combo box */
	TSharedPtr<SComboBox<TSharedPtr<FName>>> ComboBox;

	/** Options source for the combo box */
	TArray<TSharedPtr<FName>> Options;

	/** If true, shows 'Multiple Values' instead of the selection */
	bool bHasMultipleValues = false;

	// Slate Args
	FOnAttributeSelectionChanged OnSelectionChanged;
};
