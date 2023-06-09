// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

enum class ECheckBoxState : uint8;
struct FDMXEntityFixturePatchRef;
class FReply;
class FUICommandList;
class SDMXControlConsoleReadOnlyFixturePatchList;
class UDMXControlConsoleFaderGroup;
class UDMXEntityFixturePatch;


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

	/** Creates a context menu for a row in the FixturePatchList widget */
	TSharedPtr<SWidget> CreateRowContextMenu();

	/** Creates a menu for the Add Patch combo button */
	TSharedRef<SWidget> CreateAddPatchMenu();

	/** Called when row selection changes in FixturePatchList */
	void OnRowSelectionChanged(const TSharedPtr<FDMXEntityFixturePatchRef> NewSelection, ESelectInfo::Type SelectInfo);

	/** Called when a row is clicked in FixturePatchList */
	void OnRowClicked(const TSharedPtr<FDMXEntityFixturePatchRef> ItemClicked);

	/** Called when a row is double clicked in FixturePatchList */
	void OnRowDoubleClicked(const TSharedPtr<FDMXEntityFixturePatchRef> ItemClicked);

	/** Called when checkbox state of the whole FixturePatchList changes */
	void OnListCheckBoxStateChanged(ECheckBoxState CheckBoxState);

	/** Called when checkbox state of a row changes in FixturePatchList */
	void OnRowCheckBoxStateChanged(ECheckBoxState CheckBoxState, const TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef);

	/** Called to get the checkbox state of the whole FixturePatchList */
	ECheckBoxState IsListChecked() const;

	/** Called to get wheter a row is checked or not in FixturePatchList */
	bool IsRowChecked(const TSharedPtr<FDMXEntityFixturePatchRef> InFixturePatchRef) const;

	/** Called to mute/unmute all Fader Groups in current Control Console */
	void OnMuteAllFaderGroups(bool bMute, bool bOnlyActive = false) const;

	/** Gets wheter any Fader Group is muted/unmuted */
	bool IsAnyFaderGroupsMuted(bool bMute, bool bOnlyActive = false) const;

	/** Reference to FixturePatchList widget */
	TSharedPtr<SDMXControlConsoleReadOnlyFixturePatchList> FixturePatchList;

	/** Command list for the Asset Combo Button */
	TSharedPtr<FUICommandList> CommandList;
};
