// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FDMXControlConsoleManager;
class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFaderGroup;


class FDMXControlConsoleSelection
	: public TSharedFromThis<FDMXControlConsoleSelection>
{
public:
	/** Constructor */
	FDMXControlConsoleSelection(const TSharedRef<FDMXControlConsoleManager>& InControlConsoleManager);

	/** Adds the given Fader Group to selection */
	void AddToSelection(UDMXControlConsoleFaderGroup* FaderGroup);

	/** Adds the given Fader to selection */
	void AddToSelection(UDMXControlConsoleFaderBase* Fader);

	/** Removes the given Fader Group from selection */
	void RemoveFromSelection(UDMXControlConsoleFaderGroup* FaderGroup);

	/** Removes the given Fader from selection */
	void RemoveFromSelection(UDMXControlConsoleFaderBase* Fader);

	/** Gets wheter the given Fader Group is selected or not */
	bool IsSelected(UDMXControlConsoleFaderGroup* FaderGroup) const;

	/** Gets wheter the given Fader is selected or not */
	bool IsSelected(UDMXControlConsoleFaderBase* Fader) const;

	/** Gets wether Fader Groups multiselecton is allowed or not */
	bool IsMultiselectAllowed() const { return bAllowMultiselect; }

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
	
	/** Gets Selected Faders array */
	TArray<TWeakObjectPtr<UObject>> GetSelectedFaders() const { return SelectedFaders; }

	/** Gets all selected Faders from the fiven Fader Group */
	TArray<UDMXControlConsoleFaderBase*> GetSelectedFadersFromFaderGroup(UDMXControlConsoleFaderGroup* FaderGroup) const;

	/** Gets a reference to OnFaderGroupSelectionChanged delegate */
	FSimpleMulticastDelegate& GetOnFaderGroupSelectionChanged() { return OnFaderGroupSelectionChanged; }

	/** Gets a reference to OnClearFaderGroupSelection delegate */
	FSimpleMulticastDelegate& GetOnClearFaderGroupSelection() { return OnClearFaderGroupSelection; }

	/** Gets a reference to OnFaderSelectionChanged delegate */
	FSimpleMulticastDelegate& GetOnFaderSelectionChanged() { return OnFaderSelectionChanged; }

	/** Gets a reference to OnClearFaderSelection delegate */
	FSimpleMulticastDelegate& GetOnClearFaderSelection() { return OnClearFaderSelection; }

private:
	/** Weak reference to DMX DMX Control Console */
	TWeakPtr<FDMXControlConsoleManager> WeakControlConsoleManager;

	/** Array of current selected Fader Groups */
	TArray<TWeakObjectPtr<UObject>> SelectedFaderGroups;

	/** Array of current selected Faders */
	TArray<TWeakObjectPtr<UObject>> SelectedFaders;

	/** Called when Fader Group selection changes */
	FSimpleMulticastDelegate OnFaderGroupSelectionChanged;

	/** Called on Fader Group selection clearing */
	FSimpleMulticastDelegate OnClearFaderGroupSelection;

	/** Called when Fader selection changes */
	FSimpleMulticastDelegate OnFaderSelectionChanged;

	/** Called on Fader selection clearing */
	FSimpleMulticastDelegate OnClearFaderSelection;

	/** Shows if Fader Groups multiselection is allowed */
	bool bAllowMultiselect = false;;
};
