// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FDMXEntityFixturePatchRef;
class SDMXReadOnlyFixturePatchList;
class UDMXControlConsoleFaderGroup;
class UDMXEntityFixturePatch;

class FReply;
class FUICommandList;


/** A container for FixturePatchRow widgets */
class SDMXControlConsoleEditorFixturePatchVerticalBox
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleEditorFixturePatchVerticalBox)
	{}

	SLATE_END_ARGS()

	/** Destructor */
	~SDMXControlConsoleEditorFixturePatchVerticalBox();

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Refreshes the widget */
	void ForceRefresh();

private:
	/** Registers commands for this widget */
	void RegisterCommands();

	/** Generates a toolbar for FixturePatchList widget */
	TSharedRef<SWidget> GenerateFixturePatchListToolbar();

	/** Creates a context menu for a row in the FixturePatchList widget */
	TSharedPtr<SWidget> CreateRowContextMenu();

	/** Creates a menu for the Add Patch combo button */
	TSharedRef<SWidget> CreateAddPatchMenu();

	/** Edits the given Fader Group according to the given Fixture Patch */
	void GenerateFaderGroupFromFixturePatch(UDMXControlConsoleFaderGroup* FaderGroup, UDMXEntityFixturePatch* FixturePatch);

	/** Called to generate fader groups from fixture patches on last row */
	void OnGenerateFromFixturePatchOnLastRow();

	/** Called to generate fader group from fixture patches on a new row */
	void OnGenerateFromFixturePatchOnNewRow();

	/** Called to generate the selected fader group from a fixture patch */
	void OnGenerateSelectedFaderGroupFromFixturePatch();

	/** Called on Add All Patches button click to generate Fader Groups form a Library */
	FReply OnAddAllPatchesClicked();

	/** Gets for each FixturePatchList row if they should be enabled or not */
	bool IsFixturePatchListRowEnabled(const FDMXEntityFixturePatchRef InFixturePatchRef) const;

	/** Gets enable state for Add All Patches button when a DMX Library is selected */
	bool IsAddAllPatchesButtonEnabled() const;

	/** Gets wheter add next action is executable or not */
	bool CanExecuteAddNext() const;

	/** Gets wheter add row action is executable or not */
	bool CanExecuteAddRow() const;

	/** Gets wheter add selected action is executable or not */
	bool CanExecuteAddSelected() const;

	/** Reference to FixturePatchList widget */
	TSharedPtr<SDMXReadOnlyFixturePatchList> FixturePatchList;

	/** Command list for the Asset Combo Button */
	TSharedPtr<FUICommandList> CommandList;
};
