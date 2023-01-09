// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FDMXControlConsoleEditorManager;
class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFaderGroup;


class FDMXControlConsoleEditorSelection
	: public TSharedFromThis<FDMXControlConsoleEditorSelection>
{
public:
	DECLARE_EVENT_OneParam(FDMXControlConsoleEditorSelection, FDMXFaderSelectionEvent, UDMXControlConsoleFaderBase*)
	DECLARE_EVENT_OneParam(FDMXControlConsoleEditorSelection, FDMXFaderGroupSelectionEvent, UDMXControlConsoleFaderGroup*)

	/** Constructor */
	FDMXControlConsoleEditorSelection(const TSharedRef<FDMXControlConsoleEditorManager>& InControlConsoleManager);

	/** Adds the given Fader Group to selection */
	void AddToSelection(UDMXControlConsoleFaderGroup* FaderGroup);

	/** Adds the given Fader to selection */
	void AddToSelection(UDMXControlConsoleFaderBase* Fader);

	/** Removes the given Fader Group from selection */
	void RemoveFromSelection(UDMXControlConsoleFaderGroup* FaderGroup);

	/** Removes the given Fader from selection */
	void RemoveFromSelection(UDMXControlConsoleFaderBase* Fader);

	/** Handles Fader Groups multiselection */
	void Multiselect(UDMXControlConsoleFaderGroup* FaderGroup);

	/** Handles Faders multiselection */
	void Multiselect(UDMXControlConsoleFaderBase* Fader);

	/** Replaces the given selected Fader Group with the next available one */
	void ReplaceInSelection(UDMXControlConsoleFaderGroup* FaderGroup);

	/** Replaces the given selected Fader with the next available one */
	void ReplaceInSelection(UDMXControlConsoleFaderBase* Fader);

	/** Gets wheter the given Fader Group is selected or not */
	bool IsSelected(UDMXControlConsoleFaderGroup* FaderGroup) const;

	/** Gets wheter the given Fader is selected or not */
	bool IsSelected(UDMXControlConsoleFaderBase* Fader) const;

	/** Sets multiselection state */
	void SetAllowMultiselect(bool bAllow);

	/** Clears from selection alla Faders */
	void ClearFadersSelection();

	/** Clears from selection alla Faders owned by the given FaderGroup */
	void ClearFadersSelection(UDMXControlConsoleFaderGroup* FaderGroup);

	/** Clears all Selected Objects arrays */
	void ClearSelection();

	/** Gets Selected Fader Gorups array */
	TArray<TWeakObjectPtr<UObject>> GetSelectedFaderGroups() const { return SelectedFaderGroups; }

	/** Gets first selected Fader Group sorted by index */
	UDMXControlConsoleFaderGroup* GetFirstSelectedFaderGroup() const;
	
	/** Gets Selected Faders array */
	TArray<TWeakObjectPtr<UObject>> GetSelectedFaders() const { return SelectedFaders; }

	/** Gets first selected Fader sorted by index */
	UDMXControlConsoleFaderBase* GetFirstSelectedFader() const;

	/** Gets all selected Faders from the fiven Fader Group */
	TArray<UDMXControlConsoleFaderBase*> GetSelectedFadersFromFaderGroup(UDMXControlConsoleFaderGroup* FaderGroup) const;

	/** Gets a reference to OnSelectionChanged delegate */
	FSimpleMulticastDelegate& GetOnSelectionChanged() { return OnSelectionChanged; }

	/** Gets a reference to OnFaderGroupSelectionChanged delegate */
	FDMXFaderGroupSelectionEvent& GetOnFaderGroupSelectionChanged() { return OnFaderGroupSelectionChanged; }

	/** Gets a reference to OnClearFaderGroupSelection delegate */
	FSimpleMulticastDelegate& GetOnClearFaderGroupSelection() { return OnClearFaderGroupSelection; }

	/** Gets a reference to OnFaderSelectionChanged delegate */
	FDMXFaderSelectionEvent& GetOnFaderSelectionChanged() { return OnFaderSelectionChanged; }

	/** Gets a reference to OnClearFaderSelection delegate */
	FSimpleMulticastDelegate& GetOnClearFaderSelection() { return OnClearFaderSelection; }

private:
	/** Weak reference to DMX DMX Control Console */
	TWeakPtr<FDMXControlConsoleEditorManager> WeakControlConsoleManager;

	/** Array of current selected Fader Groups */
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroups;

	/** Array of current selected Faders */
	TArray<TWeakObjectPtr<UObject>> SelectedFaders;

	/** Called whenever current selection changes */
	FSimpleMulticastDelegate OnSelectionChanged;

	/** Called when Fader Group selection changes */
	FDMXFaderGroupSelectionEvent OnFaderGroupSelectionChanged;

	/** Called on Fader Group selection clearing */
	FSimpleMulticastDelegate OnClearFaderGroupSelection;

	/** Called when Fader selection changes */
	FDMXFaderSelectionEvent OnFaderSelectionChanged;

	/** Called on Fader selection clearing */
	FSimpleMulticastDelegate OnClearFaderSelection;
};
