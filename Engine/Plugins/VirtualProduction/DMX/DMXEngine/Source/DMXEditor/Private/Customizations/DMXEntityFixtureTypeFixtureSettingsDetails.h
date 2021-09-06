// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FDMXEditor;


/** Details customization for the 'FixtureType FunctionProperties' details view */
class FDMXEntityFixtureTypeFixtureSettingsDetails
	: public IDetailCustomization
{
public:
	/** Constructor */
	FDMXEntityFixtureTypeFixtureSettingsDetails(TWeakPtr<FDMXEditor> InDMXEditorPtr)
		: DMXEditorPtr(InDMXEditorPtr)
	{}

	/** Creates an instance of this details customization */
	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<FDMXEditor> InDMXEditorPtr);

	//~ Begin IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization interface

protected:
	/** Weak reference to the DMX editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
