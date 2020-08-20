// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Layout/Visibility.h"

class FDMXEditor;
class FDMXFixtureModeItem;
class FDMXFixtureFunctionItem;
class FDMXFixtureTypeSharedData;
class UDMXEntityFixtureType;

class IPropertyHandle;
class IPropertyUtilities;



/** Details customization for the 'FixtureType FunctionProperties' details view */
class FDMXEntityFixtureTypeFunctionPropertiesDetails
	: public IDetailCustomization
{
public:
	/** Constructor */
	FDMXEntityFixtureTypeFunctionPropertiesDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

protected:
	/** Called when modes were selected */
	void OnModesSelected();

	/** Called when modes were selected */
	void OnFunctionsSelected();

	/** Called when Modes being edited changed */
	void OnNumModesChanged();

	/** Called when Functions being edited changed */
	void OnNumFunctionsChanged();

	/** Called to determine wether the properties of a Function should be visibile */
	EVisibility GetFunctionVisibility(TSharedPtr<FDMXFixtureFunctionItem> FunctionItem) const;

protected:
	/** Cached Property Utilities for this Details Customization */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Shared data for fixture types */
	TSharedPtr<FDMXFixtureTypeSharedData> SharedData;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
