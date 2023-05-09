// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

class UDMXControlConsole;
class UDMXControlConsoleData;
class UDMXControlConsoleEditorModel;
class FDMXControlConsoleEditorSelection;


/** Enum for DMX Control Console widgets layout modes */
enum class EDMXControlConsoleEditorViewMode : uint8
{
	Collapsed,
	Expanded
};

/** Manages lifetime and provides access to the DMX Control Console */
class FDMXControlConsoleEditorManager final
	: public TSharedFromThis<FDMXControlConsoleEditorManager>
{
public:
	/** Destructor */
	~FDMXControlConsoleEditorManager();

	/** Gets a reference to DMX Editor's DMX Control Console manager */
	static FDMXControlConsoleEditorManager& Get();

	/** Returns the Control Console currently being edited, or nullptr if no console is being edited. */
	UDMXControlConsole* GetEditorConsole() const;

	/** Gets data of the Control Console currently being edited */
	UDMXControlConsoleData* GetEditorConsoleData() const;

	/** Gets a reference to the Selection Handler*/
	TSharedRef<FDMXControlConsoleEditorSelection> GetSelectionHandler();

	/**Gets the current View Mode for Fader Groups. */
	EDMXControlConsoleEditorViewMode GetFaderGroupsViewMode() const { return FaderGroupsViewMode; }

	/** Gets the current View Mode for Faders. */
	EDMXControlConsoleEditorViewMode GetFadersViewMode() const { return FadersViewMode; }

	/** Sets the current View Mode for Fader Groups. */
	void SetFaderGroupsViewMode(EDMXControlConsoleEditorViewMode ViewMode);

	/** Sets the current View Mode for Faders. */
	void SetFadersViewMode(EDMXControlConsoleEditorViewMode ViewMode);

	/** Sends DMX on the Control Console */
	void SendDMX();

	/** Stops sending DMX data from the Control Console */
	void StopDMX();

	/** Gets wheter the Control Console is sending DMX data or not */
	bool IsSendingDMX() const;

	/** True if DMX data sending can be played */
	bool CanSendDMX() const { return !IsSendingDMX(); }

	/** True if DMX data sending can be stopped */
	bool CanStopDMX() const { return IsSendingDMX(); };

	/** Resets DMX Control Console */
	void ClearAll();

	/** Gets a reference to OnFaderGroupsViewModeChanged delegate */
	FSimpleMulticastDelegate& GetOnFaderGroupsViewModeChanged() { return OnFaderGroupsViewModeChanged; }

	/** Gets a reference to OnFadersViewModeChanged delegate */
	FSimpleMulticastDelegate& GetOnFadersViewModeChanged() { return OnFadersViewModeChanged; }

private:
	/** Private constructor. Use FDMXControlConsoleManager::Get() instead. */
	FDMXControlConsoleEditorManager();

	/** Called before the engine is shut down */
	void OnEnginePreExit();

	/** Current view mode for FaderGroupView widgets*/
	EDMXControlConsoleEditorViewMode FaderGroupsViewMode = EDMXControlConsoleEditorViewMode::Collapsed;

	/** Current view mode for Faders widgets */
	EDMXControlConsoleEditorViewMode FadersViewMode = EDMXControlConsoleEditorViewMode::Collapsed;

	/** Called when the Fader Groups view mode is changed */
	FSimpleMulticastDelegate OnFaderGroupsViewModeChanged;

	/** Called when the Faders view mode is changed */
	FSimpleMulticastDelegate OnFadersViewModeChanged;

	/** Selection for the DMX Control Console */
	TSharedPtr<FDMXControlConsoleEditorSelection> SelectionHandler;

	/** The DMX Control Console manager instance */
	static TSharedPtr<FDMXControlConsoleEditorManager> Instance;
};
