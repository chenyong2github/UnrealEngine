// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "UObject/WeakObjectPtr.h"

struct FDMXEntityFixturePatchRef;
class SDMXControlConsoleDetailsRowWidget;
class SDMXControlConsolePortSelector;
class UDMXLibrary;
class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFaderGroup;

struct EVisibility;
class FReply;
class IDetailsView;
class IPropertyUtilities;


/** Details Customization for DMX DMX Control Console */
class FDMXControlConsoleDetails
	: public IDetailCustomization
{
public:
	/** Makes an instance of this Details Customization */
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FDMXControlConsoleDetails>();
	}

	//~ Begin of IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailLayout) override;
	//~ End of IDetailCustomization interface

private:
	/** Generates the Port Selector for the DMX Control Console being edited */
	void GeneratePortSelectorRow(IDetailLayoutBuilder& InDetailLayout);

	/** Generates fixture patch details rows for the current DMX Control Console's DMX Library */
	void GenerateFixturePatchDetailsRows(IDetailLayoutBuilder& InDetailLayout);

	/** Called when a fixture patch row is selected */
	void OnSelectFixturePatchDetailsRow(const TSharedRef<SDMXControlConsoleDetailsRowWidget>& DetailsRow);

	/** Called to generate a fader group from a fixture patch */
	void OnGenerateFaderGroupFromFixturePatch(const TSharedRef<SDMXControlConsoleDetailsRowWidget>& DetailsRow);

	/** Updates DMX Library's fixture patch list */
	void UpdateFixturePatches();

	/** Forces a refresh on the entire Details View */
	void ForceRefresh() const;

	/** Called when Port selection changes */
	void OnSelectedPortsChanged();

	/** Called on Add All Patches button click to generate Fader Groups form a Library */
	FReply OnAddAllPatchesClicked();

	/** Gets visibility for Add All Patches button when a DMX Library is selected */
	EVisibility GetAddAllPatchesButtonVisibility() const;

	/** Widget to handle Port selection */
	TSharedPtr<SDMXControlConsolePortSelector> PortSelector;

	/** Array of Fixture Patches in the current DMX Library */
	TArray<FDMXEntityFixturePatchRef> FixturePatches;

	/** Array of weak references to fixture patch details rows */
	TArray<TWeakPtr<SDMXControlConsoleDetailsRowWidget>> FixturePatchDetailsRowWidgets;

	/** Current selected fixture patch details row */
	TWeakPtr<SDMXControlConsoleDetailsRowWidget> SelectedDetailsRowWidget;

	/** Current DMX Library selected in the DMX Control Console */
	TWeakObjectPtr<UDMXLibrary> DMXLibrary;

	/** Property Utilities for this Details Customization layout */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
