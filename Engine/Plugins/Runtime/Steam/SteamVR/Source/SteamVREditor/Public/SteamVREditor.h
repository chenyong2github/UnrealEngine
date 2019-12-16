// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "../../SteamVRInputDevice/Public/SteamVRInputDevice.h"
#include "../../SteamVRInputDevice/Public/SteamVRInputDeviceFunctionLibrary.h"

class FToolBarBuilder;
class FMenuBuilder;
class SWidget;

class FSteamVREditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** These functions will be bound to Commands */
	void PluginButtonClicked();
	
	void JsonRegenerateActionManifest();
	void JsonRegenerateControllerBindings();
	void ReloadActionManifest();
	void LaunchBindingsURL();
	void AddSampleInputs();

private:

	void AddToolbarExtension(FToolBarBuilder& Builder);
	void AddMenuExtension(FMenuBuilder& Builder);
	bool AddUniqueAxisMapping(TArray<FInputAxisKeyMapping> ExistingAxisKeys, UInputSettings* InputSettings, FName ActionName, FKey ActionKey);
	bool AddUniqueActionMapping(TArray<FInputActionKeyMapping> ExistingActionKeys, UInputSettings* InputSettings, FName ActionName, FKey ActionKey);

	TSharedPtr<class FUICommandList> PluginCommands;
	TSharedRef<SWidget> FillComboButton(TSharedPtr<class FUICommandList> Commands);

};
