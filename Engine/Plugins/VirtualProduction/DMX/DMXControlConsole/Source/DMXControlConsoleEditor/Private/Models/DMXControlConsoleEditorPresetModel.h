// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXControlConsole.h"

#include "UObject/Object.h"

#include "DMXControlConsoleEditorPresetModel.generated.h"

class UDMXControlConsolePreset;


/** Model of the preset currently being edited in the control console editor.  */
UCLASS(Config = DMXEditor)
class UDMXControlConsoleEditorPresetModel final
	: public UObject
{
	GENERATED_BODY()

public:
	/** Returns the editor preset */
	UDMXControlConsolePreset* GetEditorPreset() const { return EditorPreset; }
	
	/** Loads the preset stored in config or creates a transient preset if none is stored in config */
	void LoadDefaultPreset();

	/** Creates a new preset in the transient package. */
	void CreateNewPreset();

	/** Saves the preset. Creates a new asset if the asset was never saved. */
	void SavePreset();

	/** Saves the preset as new asset. */
	void SavePresetAs();

	/** Loads a preset. Returns the loaded preset, or nullptr if the preset could not be loaded. */
	void LoadPreset(const FAssetData& AssetData);

	/** 
	 * Creates a new Control Console Preset asset from provided source control console in desired package name.
	 * 
	 * @param SavePackagePath				The package path
	 * @param SaveAssetName					The desired asset name
	 * @param SourceControlConsole			The control console copied into the new preset
	 * @return								The newly created preset, or nullptr if no preset could be created
	 */
	[[nodiscard]] UDMXControlConsolePreset* CreateNewPresetAsset(const FString& SavePackagePath, const FString& SaveAssetName, UDMXControlConsole* SourceControlConsole) const;

	/** Returns a delegate broadcast whenever a preset is loaded */
	FSimpleMulticastDelegate& GetOnPresetLoaded() { return OnPresetLoadedDelegate; }

protected:
	//~ Begin UObject interface
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
	//~ End UObject interface

private:
	/** Saves the current editor preset to the Control Console Editor Setting config */
	void SavePresetToConfig();

	/** Finalizes loading a  preset. Useful to pass in the result of a dialog. */
	void FinalizeLoadPreset(UDMXControlConsolePreset* PresetToLoad);

	/** Called when enter pressed a preset in the Load Dialog */
	void OnLoadDialogEnterPressedPreset(const TArray<FAssetData>& InPresetAssets);
	
	/** Called when the load dialog selected a preset */
	void OnLoadDialogSelectedPreset(const FAssetData& PresetAsset);

	/** Opens a Save Dialog, returns true if the user's dialog interaction results in a valid OutPackageName. */
	[[nodiscard]] bool OpenSaveDialog(const FString& InDefaultPath, const FString& InNewNameSuggestion, FString& OutPackageName) const;

	/** Prompts user to specify a preset package name to save to. Returns true if a valid package name was acquired. */
	[[nodiscard]] bool PromptSavePresetPackage(FString& OutSavePackagePath, FString& OutSaveAssetName) const;

	/** Called at the very end of engine initialization, right before the engine starts ticking. */
	void OnFEngineLoopInitComplete();

	/** Delegate that needs be broadcast whenever a new preset is loaded */
	FSimpleMulticastDelegate OnPresetLoadedDelegate;

	/** The current preset */
	UPROPERTY(Transient)
	TObjectPtr<UDMXControlConsolePreset> EditorPreset;

	/** Saved control console preset */
	UPROPERTY(Config)
	FSoftObjectPath SavedPreset;
};
