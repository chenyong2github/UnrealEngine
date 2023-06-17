// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXControlConsole.h"

#include "DMXControlConsoleData.h"
#include "UObject/Object.h"
#include "Widgets/SDMXReadOnlyFixturePatchList.h"

#include "DMXControlConsoleEditorModel.generated.h"

class FDMXControlConsoleEditorSelection;
struct FDMXReadOnlyFixturePatchListDescriptor;
class UDMXControlConsole;
class UDMXControlConsoleData;
class UDMXControlConsoleFaderGroup;

namespace UE::DMXControlConsoleEditor::FilterModel::Private
{
	class FFilterModel;
}


/** Enum for DMX Control Console widgets layout modes */
enum class EDMXControlConsoleEditorViewMode : uint8
{
	Collapsed,
	Expanded
};

/** Enum for DMX Control Console input modes */
enum class EDMXControlConsoleEditorInputMode : uint8
{
	Relative,
	Absolute
};

/** Model of the console currently being edited in the control console editor.  */
UCLASS(Config = DMXEditor)
class UDMXControlConsoleEditorModel 
	: public UObject
{
	GENERATED_BODY()

	/** Allow filter model to getter itself */
	friend UE::DMXControlConsoleEditor::FilterModel::Private::FFilterModel;

public:
	/** Returns the edited console */
	UDMXControlConsole* GetEditorConsole() const { return EditorConsole; }

	/** Returns the edited console data */
	UDMXControlConsoleData* GetEditorConsoleData() const { return EditorConsole->GetControlConsoleData(); }

	/** Gets a reference to the Selection Handler*/
	TSharedRef<FDMXControlConsoleEditorSelection> GetSelectionHandler();

	/**Gets the current View Mode for Fader Groups. */
	EDMXControlConsoleEditorViewMode GetFaderGroupsViewMode() const { return FaderGroupsViewMode; }

	/** Gets the current View Mode for Faders. */
	EDMXControlConsoleEditorViewMode GetFadersViewMode() const { return FadersViewMode; }

	/** Gets the current Input Mode for Faders. */
	EDMXControlConsoleEditorInputMode GetInputMode() const { return InputMode; }

	/** Sets the current View Mode for Fader Groups. */
	void SetFaderGroupsViewMode(EDMXControlConsoleEditorViewMode ViewMode);

	/** Sets the current View Mode for Faders. */
	void SetFadersViewMode(EDMXControlConsoleEditorViewMode ViewMode);

	/** Sets the current Input Mode for Faders. */
	void SetInputMode(EDMXControlConsoleEditorInputMode NewInputMode) { InputMode = NewInputMode; }

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

	/** Removes all selected elements from DMX Control Console */
	void RemoveAllSelectedElements();

	/** Resets DMX Control Console */
	void ClearAll();

	/** Scrolls the given FaderGroup into view */
	void ScrollIntoView(const UDMXControlConsoleFaderGroup* FaderGroup) const;

	/** Loads the console stored in config or creates a transient console if none is stored in config */
	void LoadConsoleFromConfig();

	/** Creates a new console in the transient package. */
	void CreateNewConsole();

	/** Saves the console. Creates a new asset if the asset was never saved. */
	void SaveConsole();

	/** Saves the console as new asset. */
	void SaveConsoleAs();

	/** Loads a console. Returns the loaded console, or nullptr if the console could not be loaded. */
	void LoadConsole(const FAssetData& AssetData);

	/** Requests refresh for the current Control Console */
	void RequestRefresh();

	/** 
	 * Creates a new Control Console asset from provided source control console in desired package name.
	 * 
	 * @param SavePackagePath				The package path
	 * @param SaveAssetName					The desired asset name
	 * @param SourceControlConsole			The control console copied into the new console
	 * @return								The newly created console, or nullptr if no console could be created
	 */
	[[nodiscard]] UDMXControlConsole* CreateNewConsoleAsset(const FString& SavePackagePath, const FString& SaveAssetName, UDMXControlConsoleData* SourceControlConsole) const;

	/** Returns the descriptor for Fixture Patch List parameters */
	const FDMXReadOnlyFixturePatchListDescriptor& GetFixturePatchListDescriptor() const { return FixturePatchListDescriptor; }

	/** Saves to config the given descriptor for Fixture Patch List parameters */
	void SaveFixturePatchListDescriptorToConfig(const FDMXReadOnlyFixturePatchListDescriptor ListDescriptor);

	/** Returns a delegate broadcast whenever a console is saved */
	FSimpleMulticastDelegate& GetOnConsoleSaved() { return OnConsoleSavedDelegate; }

	/** Returns a delegate broadcast whenever a console is loaded */
	FSimpleMulticastDelegate& GetOnConsoleLoaded() { return OnConsoleLoadedDelegate; }

	/** Gets a reference to OnFaderGroupsViewModeChanged delegate */
	FSimpleMulticastDelegate& GetOnFaderGroupsViewModeChanged() { return OnFaderGroupsViewModeChanged; }

	/** Gets a reference to OnFadersViewModeChanged delegate */
	FSimpleMulticastDelegate& GetOnFadersViewModeChanged() { return OnFadersViewModeChanged; }

	/** Gets a reference to OnScrollFaderGroupIntoView delegate */
	FDMXControlConsoleFaderGroupDelegate& GetOnScrollFaderGroupIntoView() { return OnScrollFaderGroupIntoView; }

	/** Gets a reference to OnControlConsoleForceRefresh delegate */
	FSimpleMulticastDelegate& GetOnControlConsoleForceRefresh() { return OnControlConsoleForceRefresh; }

protected:
	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
	//~ End UObject interface

private:
	/** Refreshes Control Console */
	void ForceRefresh();

	/** Saves the current editor console to config */
	void SaveConsoleToConfig();

	/** Finalizes loading a  console. Useful to pass in the result of a dialog. */
	void FinalizeLoadConsole(UDMXControlConsole* ControlConsoleToLoad);

	/** Called when enter pressed a console in the Load Dialog */
	void OnLoadDialogEnterPressedConsole(const TArray<FAssetData>& ControlConsoleAssets);
	
	/** Called when the load dialog selected a console */
	void OnLoadDialogSelectedConsole(const FAssetData& ControlConsoleAssets);

	/** Opens a Save Dialog, returns true if the user's dialog interaction results in a valid OutPackageName. */
	[[nodiscard]] bool OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName) const;

	/** Prompts user to specify a console package name to save to. Returns true if a valid package name was acquired. */
	[[nodiscard]] bool PromptSaveConsolePackage(FString& OutSavePackagePath, FString& OutSaveAssetName) const;

	/** Called at the very end of engine initialization, right before the engine starts ticking. */
	void OnFEngineLoopInitComplete();

	/** Called before the engine is shut down */
	void OnEnginePreExit();

	/** Delegate that needs be broadcast whenever a console is saved */
	FSimpleMulticastDelegate OnConsoleSavedDelegate;

	/** Delegate that needs be broadcast whenever a new console is loaded */
	FSimpleMulticastDelegate OnConsoleLoadedDelegate;

	/** Called when the Fader Groups view mode is changed */
	FSimpleMulticastDelegate OnFaderGroupsViewModeChanged;

	/** Called when the Faders view mode is changed */
	FSimpleMulticastDelegate OnFadersViewModeChanged;

	/** Called when a Fader Group needs to be scrolled into view */
	FDMXControlConsoleFaderGroupDelegate OnScrollFaderGroupIntoView;

	/** Called when Control Console needs to be refreshed */
	FSimpleMulticastDelegate OnControlConsoleForceRefresh;

	/** Timer handle in use while refreshing Control Console is requested but not carried out yet */
	FTimerHandle ForceRefreshTimerHandle;

	/** The filter model for this console editor */
	TSharedPtr<UE::DMXControlConsoleEditor::FilterModel::Private::FFilterModel> FilterModel;

	/** Selection for the DMX Control Console */
	TSharedPtr<FDMXControlConsoleEditorSelection> SelectionHandler;

	/** The currently edited console */
	UPROPERTY(Transient)
	TObjectPtr<UDMXControlConsole> EditorConsole;

	/** Control console saved in config */
	UPROPERTY(Config)
	FSoftObjectPath DefaultConsolePath;

	/** Fixture Patch List default descriptor saved in config */
	UPROPERTY(Config)
	FDMXReadOnlyFixturePatchListDescriptor FixturePatchListDescriptor;

	/** Current view mode for FaderGroupView widgets*/
	EDMXControlConsoleEditorViewMode FaderGroupsViewMode = EDMXControlConsoleEditorViewMode::Expanded;

	/** Current view mode for Faders widgets */
	EDMXControlConsoleEditorViewMode FadersViewMode = EDMXControlConsoleEditorViewMode::Collapsed;
	
	/** Current input mode for Faders widgets */
	EDMXControlConsoleEditorInputMode InputMode = EDMXControlConsoleEditorInputMode::Absolute;
};
