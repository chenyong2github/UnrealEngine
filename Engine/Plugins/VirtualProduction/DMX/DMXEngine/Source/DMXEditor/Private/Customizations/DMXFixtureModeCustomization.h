// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FDMXEditor;
class UDMXEntityFixtureType;

class SEditableTextBox;


/** 
 * Customization for the DMXFixtureMode struct 
 */
class FDMXFixtureModeCustomization
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
	/** Builds the widget that displays the Mode name */
	void BuildModeNameWidget(IDetailChildrenBuilder& InStructBuilder);
	
	/** Find the existing names for the Mode type being edited within the Fixture Type the Mode resides in */
	TArray<FString> GetExistingModeNames() const;

	/** Called when the name of a Mode changed */
	void OnModeNameChanged(const FText& InNewText);

	/** Called when the name of a mode changed */
	void OnModeNameCommitted(const FText& InNewText, ETextCommit::Type InTextCommit);

	/** Returns the Mode name */
	FText GetModeName() const;

	/** Changes the Mode name on the fixture properties */
	void SetModeName(const FString& NewName);

	/** Returns the visibility of the Fixture Matrix property */
	EVisibility GetFixtureMatrixPropertyVisibility() const;

	/** Handle to the Mode Name property */
	TSharedPtr<IPropertyHandle> ModeNameHandle;

	/** The text box to edit the Mode name */
	TSharedPtr<SEditableTextBox> ModeNameEditableTextBox;

	/** The Fixture Type that owns the Mode */
	TWeakObjectPtr<UDMXEntityFixtureType> OuterFixtureType;
};
