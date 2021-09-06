// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"

#include "Library/DMXEntityFixtureType.h"

#include "CoreMinimal.h"
#include "Layout/Visibility.h"

class FDMXEditor;
class FDMXFixtureModeItem;
class FDMXFixtureTypeSharedData;
class UDMXEntityFixtureType;
class IPropertyHandle;
class IPropertyUtilities;


/** Details customization for the 'FixtureType ModeProperties' details view */
class FDMXEntityFixtureTypeModePropertiesDetails
	: public IDetailCustomization
{
public:
	/** Constructor */
	FDMXEntityFixtureTypeModePropertiesDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}

	/** Creates an instance of this details customization */
	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<FDMXEditor> InDMXEditorPtr);
	
	//~ Begin IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization interface

protected:
	/** Called when modes were selected */
	void OnModesSelected();

	/** Called when the number of modes in the modes array changed */
	void OnNumModesChanged();

	/** Called to determine wether the properties of a Mode should be visibile */
	EVisibility GetModeVisibility(TSharedPtr<FDMXFixtureModeItem> ModeItem) const;

protected:
	/** Cached Property Utilities for this Details Customization */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;

	/** Shared data for fixture types */
	TSharedPtr<FDMXFixtureTypeSharedData> SharedData;

	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
