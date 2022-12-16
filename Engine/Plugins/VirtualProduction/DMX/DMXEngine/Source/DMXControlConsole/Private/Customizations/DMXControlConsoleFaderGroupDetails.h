// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FDMXControlConsoleManager;
class UDMXControlConsoleFaderGroup;

struct EVisibility;
class FReply;
class IPropertyHandle;
class IPropertyUtilities;


/** Details Customization for DMX DMX Control Console */
class FDMXControlConsoleFaderGroupDetails
	: public IDetailCustomization
{
public:
	/** Makes an instance of this Details Customization */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FDMXControlConsoleFaderGroupDetails>();
	}

	//~ Begin of IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;
	//~ End of IDetailCustomization interface

private:
	/** Forces refresh of the entire Details View */
	void ForceRefresh() const;

	/** True if at least one selected Fader Group have any Fixture Patch bound */
	bool DoSelectedFaderGroupsHaveAnyFixturePatches() const;

	/** Called when Clear button is clicked */
	FReply OnClearButtonClicked();

	/** Gets visibility attribute of the Editor Color Property */
	EVisibility GetEditorColorVisibility() const;

	/** Gets visibility attribute of the Clear button */
	EVisibility GetClearButtonVisibility() const;

	/** Property Handle of FixturePatchRef property of the current customized Fader Group */
	TSharedPtr<IPropertyHandle> FixturePatchRefHandle;

	/** Property Utilities for this Details Customization layout */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
