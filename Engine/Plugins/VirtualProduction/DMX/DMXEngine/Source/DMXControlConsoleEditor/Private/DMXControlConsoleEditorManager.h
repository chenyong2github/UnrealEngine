// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

class FDMXControlConsoleEditorSelection;
class UDMXControlConsole;
class UDMXControlConsolePreset;


/** Manages lifetime and provides access to the DMX Control Console */
class FDMXControlConsoleEditorManager final
	: public FGCObject
	, public TSharedFromThis<FDMXControlConsoleEditorManager>
{
public:
	/** Destructor */
	~FDMXControlConsoleEditorManager();

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FDMXControlConsoleEditorManager");
	}
	//~ End FGCObject interface

	/** Gets a reference to DMX Editor's DMX Control Console manager */
	static FDMXControlConsoleEditorManager& Get();

	/** Gets a reference to the DMX Control Console */
	UDMXControlConsole* GetDMXControlConsole() const { return ControlConsole; }

	/** Gets a reference to the Selection Handler*/
	TSharedRef<FDMXControlConsoleEditorSelection> GetSelectionHandler();

	/** Gets the Control Console Preset. Returns nullptr if no preset is loaded. */
	UDMXControlConsolePreset* GetPreset() const;

	/** Creates a new Control Console in the Transient Package */
	UDMXControlConsole* CreateNewTransientConsole();

	/** Creates a new Control Console Preset asset at the given path. If no Control Console is provided, it uses the current Control Console */
	UDMXControlConsolePreset* CreateNewPresetAsset(FString DesiredPackageName, UDMXControlConsole* SourceControlConsole = nullptr);

	/** Saves the current Control Console Preset to the current preset. */
	void Save();

	/** Saves the current Control Console as a Preset asset via a save dialog. */
	void SaveAs();

	/** Loads a Control Console Preset via a load dialog. */
	void Load();

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

	/** Gets a reference to OnControlConsoleLoaded delegate */
	FSimpleMulticastDelegate& GetOnControlConsoleLoaded() { return OnControlConsoleLoaded; }

private:
	/** Private constructor. Use FDMXControlConsoleManager::Get() instead. */
	FDMXControlConsoleEditorManager();
	
	/** Called when enter pressed a preset in the Load Dialog */
	void OnLoadDialogEnterPressedPreset(const TArray<FAssetData>& PresetAssets);

	/** Called when the load dialog selected a preset */
	void OnLoadDialogSelectedPreset(const FAssetData& PresetAsset);

	/** Opens a Save Dialog, returns true if the user's dialog interaction results in a valid OutPackageName. */
	bool OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName) const;

	/** Gets a preset package name to save to. Returns true if a valid package name was acquired. */
	bool GetSavePresetPackageName(FString& OutPackageName) const;

	/** Saves the configuration of this manager */
	void SaveConfig();

	/** Loads the configuration of this manager */
	void LoadConfig();

	/** Called at the very end of engine initialization, right before the engine starts ticking. */
	void OnFEngineLoopInitComplete();

	/** Called before the engine is shut down */
	void OnEnginePreExit();

	/** Called when the Control Console is restored by a preset */
	FSimpleMulticastDelegate OnControlConsoleLoaded;

	/** Reference to the DMX Control Console */
	TObjectPtr<UDMXControlConsole> ControlConsole;

	/** Selection for the DMX Control Console */
	TSharedPtr<FDMXControlConsoleEditorSelection> SelectionHandler;

	/** The DMX Control Console manager instance */
	static TSharedPtr<FDMXControlConsoleEditorManager> Instance;
};
