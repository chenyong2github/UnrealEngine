// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "OSCServer.h"
#include "VPCustomUIHandler.h"

DECLARE_LOG_CATEGORY_EXTERN(LogVPUtilitiesEditor, Log, Log);

class UOSCServer;

class FVPUtilitiesEditorModule : public IModuleInterface
{
public:
		//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface


	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FVPUtilitiesEditorModule& Get()
	{
		static const FName ModuleName = "VPUtilitiesEditor";
		return FModuleManager::LoadModuleChecked<FVPUtilitiesEditorModule>(ModuleName);
	}

	/**
	 * Get an OSC server that can be started at the module's startup.
	 */
	UOSCServer* GetOSCServer() const;

private:
	/** Register VPUtilities settings. */
	void RegisterSettings();

	/** Unregister VPUtilities settings */
	void UnregisterSettings();

	/** Start an OSC server and bind a an OSC listener to it. */
	void InitializeOSCServer();

	/** Handler for when VP utilities settings are changed. */
	bool OnSettingsModified();

private:
	/** The default OSC server. */
	TStrongObjectPtr<UOSCServer> OSCServer;

	/** Virtual production role identifier for the notification bar. */
	static const FName VPRoleNotificationBarIdentifier;

	/** UI Handler for virtual scouting. */
	TStrongObjectPtr<UVPCustomUIHandler> CustomUIHandler;
};