// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FDMXEditor;
class UDMXEntityFixtureType;

struct EVisibility;
class SEditableTextBox;


/** 
 * Customization for the DMXFixtureFunction struct 
 */
class FDMXFixtureFunctionCustomization
	: public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

protected:
	//~ Begin IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InStructPropertyHandle, class FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, IDetailChildrenBuilder& InStructBuilder, IPropertyTypeCustomizationUtils& InStructCustomizationUtils) override;
	//~ End IPropertyTypeCustomization interface
	
private:
	////////////////////////////////////////
	// FunctionName property related methods

	/** Builds the widget that displays the FunctionName property */
	void BuildFunctionNameWidget(IDetailChildrenBuilder& InStructBuilder);
	
	/** Find the existing names for the function type being edited within the Fixture Type the function resides in */
	TArray<FString> GetExistingFunctionNames() const;

	/** Called when the name of a function changed */
	void OnFunctionNameChanged(const FText& InNewText);

	/** Called when the name of a mode changed */
	void OnFunctionNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit);

	/** Returns the function name */
	FText GetFunctionName() const;

	/** Changes the function name on the fixture properties */
	void SetFunctionName(const FString& NewName);

	////////////////////////////////////////
	// DefaultValue property related methods

	/** Adds fields for each byte of the default value, which will be displayed depending on the selected DataType */
	void AddDefaultValueFields(IDetailChildrenBuilder& InStructBuilder);

	/** Creates a single default value field */
	TSharedRef<SWidget> CreateSingleDefaultValueChannelField(uint8 Channel, const FLinearColor& LabelColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));

	/** Gets the value of the specified default value channel */
	TOptional<uint8> GetDefaultValueChannelValue(uint8 Channel) const;

	/** Returns the visibility of specified channel of the default value */
	EVisibility GetDefaultValueChannelVisibility(uint8 Channel) const;

	/** Called when a default value channel changed */
	void OnDefaultValueChannelValueChanged(uint8 NewValue, uint8 Channel);

	/** Called when value was committed to a default value channel */
	void OnDefaultValueChannelValueCommitted(uint8 NewValue, ETextCommit::Type CommitType);

private:
	/** Handle for the FunctionName property */
	TSharedPtr<IPropertyHandle> FunctionNameHandle;

	/** Handle for the DataType property */
	TSharedPtr<IPropertyHandle> DataTypeHandle;

	/** Handle for the DefaultValue property */
	TSharedPtr<IPropertyHandle> DefaultValueHandle;

	/** Handle for the bUseLSB property */
	TSharedPtr<IPropertyHandle> UseLSBHandle;

	/** The text box to edit the function name */
	TSharedPtr<SEditableTextBox> FunctionNameEditableTextBox;
};
