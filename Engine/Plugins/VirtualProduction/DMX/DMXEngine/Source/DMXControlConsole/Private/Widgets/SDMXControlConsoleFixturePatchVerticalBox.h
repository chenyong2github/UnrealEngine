// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FDMXEntityFixturePatchRef;
class SDMXControlConsoleFixturePatchRowWidget;
class UDMXControlConsoleFaderGroup;
class UDMXEntityFixturePatch;
class UDMXLibrary;

class FReply;
class SVerticalBox;


/** A container for FixturePatchRow widgets */
class SDMXControlConsoleFixturePatchVerticalBox
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleFixturePatchVerticalBox)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Updates FixturePatchRowsVerticalBox widget */
	void UpdateFixturePatchRows();

private:
	/** Updates FixturePatches array */
	void UpdateFixturePatches();

	/** Edits the given Fader Group according to the given Fixture Patch */
	void GenerateFaderGroupFromFixturePatch(UDMXControlConsoleFaderGroup* FaderGroup, UDMXEntityFixturePatch* FixturePatch);

	/** Called when a fixture patch row is selected */
	void OnSelectFixturePatchDetailsRow(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow);

	/** Called to generate the a fader group from a fixture patch on last row */
	void OnGenerateFromFixturePatchOnLastRow(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow);

	/** Called to generate the a fader group from a fixture patch on a new row */
	void OnGenerateFromFixturePatchOnNewRow(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow);

	/** Called to generate the selected fader group from a fixture patch */
	void OnGenerateSelectedFaderGroupFromFixturePatch(const TSharedRef<SDMXControlConsoleFixturePatchRowWidget>& FixturePatchRow);

	/** Called on Add All Patches button click to generate Fader Groups form a Library */
	FReply OnAddAllPatchesClicked();

	/** Gets visibility for Add All Patches button when a DMX Library is selected */
	EVisibility GetAddAllPatchesButtonVisibility() const;

	/** Widget to list all Fixture Patches of Control Console's current DMX Library */
	TSharedPtr<SVerticalBox> FixturePatchRowsBoxWidget;

	/** Array of Fixture Patches in the current DMX Library */
	TArray<FDMXEntityFixturePatchRef> FixturePatches;

	/** Array of weak references to fixture patch details rows */
	TArray<TWeakPtr<SDMXControlConsoleFixturePatchRowWidget>> FixturePatchRowWidgets;

	/** Current selected fixture patch details row */
	TWeakPtr<SDMXControlConsoleFixturePatchRowWidget> SelectedFixturePatchRowWidget;

	/** Current displayed DMX Library */
	TWeakObjectPtr<UDMXLibrary> DMXLibrary;
};
