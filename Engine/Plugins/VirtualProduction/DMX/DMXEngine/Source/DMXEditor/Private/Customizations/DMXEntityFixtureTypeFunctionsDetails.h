// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "CoreMinimal.h"

class FDMXEditor;
class FDMXFixtureTypeSharedData;

class IPropertyUtilities;
class IPropertyHandleArray;

class SWidget;
class FReply;

/** Details customization for 'FixtureType Functions' Details View, displaying function names in a list */
class FDMXEntityFixtureTypeFunctionsDetails
	: public IDetailCustomization
{
public:
	/** Constructor */
	FDMXEntityFixtureTypeFunctionsDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
protected:
	/** Called when num modes changed */
	void OnNumModesChanged();

	/** Creates the 'Add New Function' Button */
	TSharedRef<SWidget> AddHeaderContent();

	/** Called to determine wether the 'Add New Function' button is enabled */
	bool GetIsAddFunctionButtonEnabled() const;

	/** Called to get the tooltip text of the 'Add New Function' button */
	FText GetAddFunctionButtonTooltipText() const;

	/** Called when the 'Add New Function' button was clicked */
	FReply OnAddFunctionButtonClicked() const;

	/** Called to get the text of the 'Add New Function' button */
	FText GetAddFunctionButtonText() const;
	
protected:
	/** Array handle array of the Fixture Types' Modes Array */
	TSharedPtr<IPropertyHandleArray> ModesHandleArray;

	/** Cached Property Utilities for this Details Customization */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Shared data for fixture types */
	TSharedPtr<FDMXFixtureTypeSharedData> SharedData;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
