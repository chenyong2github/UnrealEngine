// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Templates/SharedPointer.h"

#include "Types/SlateEnums.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/STextComboBox.h"

/** Copy Material node details panel. Hides all properties from the inheret Material node. */
class FCustomizableObjectNodeTableDetails : public IDetailCustomization
{
public:
	
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** Hides details copied from CustomizableObjectNodeMaterial. */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	// Generates columns combobox options
	void GenerateColumnComboBoxOptions();
	
	// Generates layouts combobox options
	void GenerateLayoutComboBoxOptions();
	
	// Generates Animation Instance combobox options
	void GenerateAnimInstanceComboBoxOptions();

	// Generates Animation Slot combobox options
	void GenerateAnimSlotComboBoxOptions();
	
	// Generates Animation Tags combobox options
	void GenerateAnimTagsComboBoxOptions();

	// OnComboBoxSelectionChanged Callback for Column ComboBox
	void OnColumnComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo);
	
	// OnComboBoxSelectionChanged Callback for Layout ComboBox
	void OnLayoutComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo);
	
	// OnComboBoxSelectionChanged Callback for AnimInstance ComboBox
	void OnAnimInstanceComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo);
	
	// OnComboBoxSelectionChanged Callback for Anim Slot ComboBox
	void OnAnimSlotComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo);

	// OnComboBoxSelectionChanged Callback for Anim Tags ComboBox
	void OnAnimTagsComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo);

private:

	// Pointer to the node represented in this details
	class UCustomizableObjectNodeTable* Node;

	// ComboBox widget to select a column from the NodeTable
	TSharedPtr<STextComboBox> ColumnComboBox;

	// Array with the name of the table columns as combobox options
	TArray<TSharedPtr<FString>> ColumnOptionNames;	
	
	// ComboBox widget to select a column from the NodeTable
	TSharedPtr<STextComboBox> LayoutComboBox;

	// Array with the name of the table columns as combobox options
	TArray<TSharedPtr<FString>> LayoutOptionNames;

	// ComboBox widget to select an Animation Instance column from the NodeTable
	TSharedPtr<STextComboBox> AnimComboBox;

	// Array with the name of the Animation Instance columns as combobox options
	TArray<TSharedPtr<FString>> AnimOptionNames;

	// ComboBox widget to select an Animation Slot column from the NodeTable
	TSharedPtr<STextComboBox> AnimSlotComboBox;

	// Array with the name of the Animation Slot columns as combobox options
	TArray<TSharedPtr<FString>> AnimSlotOptionNames;

	// ComboBox widget to select an Animation Tags column from the NodeTable
	TSharedPtr<STextComboBox> AnimTagsComboBox;

	// Array with the name of the Animation Tags columns as combobox options
	TArray<TSharedPtr<FString>> AnimTagsOptionNames;

};