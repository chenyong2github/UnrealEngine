// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Toolkits/IToolkit.h"

class FConsoleVariablesEditorToolkit;
class ISettingsSection;
class UConsoleVariablesEditorProjectSettings;

class FConsoleVariablesEditorModule : public IModuleInterface
{
public:
	
	static FConsoleVariablesEditorModule& Get();

	//~ Begin IModuleInterface Interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface Interface

	void OpenConsoleVariablesDialogWithAssetSelected(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost, const FAssetData& InAssetData);
	
	static void OpenConsoleVariablesSettings();

	TWeakObjectPtr<UConsoleVariablesEditorProjectSettings> GetConsoleVariablesUserSettings() const
	{
		return ProjectSettingsObjectPtr;
	}

private:
	
	void PostEngineInit();

	void RegisterMenuItem();
	bool RegisterProjectSettings();
	bool HandleModifiedProjectSettings();

	void OpenConsoleVariablesEditor(const EToolkitMode::Type Mode, const TSharedPtr<class IToolkitHost>& InitToolkitHost);

	/* Lives for as long as the UI is open. */
	TWeakPtr<FConsoleVariablesEditorToolkit> ConsoleVariablesEditorToolkit;

	TSharedPtr<ISettingsSection> ProjectSettingsSectionPtr;
	TWeakObjectPtr<UConsoleVariablesEditorProjectSettings> ProjectSettingsObjectPtr;
};
