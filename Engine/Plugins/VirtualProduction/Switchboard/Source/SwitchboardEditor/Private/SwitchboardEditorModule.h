// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/IDelegateInstance.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"


DECLARE_LOG_CATEGORY_EXTERN(LogSwitchboardPlugin, Log, All);


class FSwitchboardEditorModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FSwitchboardEditorModule& Get()
	{
		static const FName ModuleName = "SwitchboardEditor";
		return FModuleManager::LoadModuleChecked<FSwitchboardEditorModule>(ModuleName);
	};

	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

#if PLATFORM_WINDOWS
	/** Returns whether (this engine's) SwitchboardListener is configured to run automatically. */
	bool IsListenerAutolaunchEnabled() const;

	/** Enables or disables auto-run of SwitchboardListener. */
	bool SetListenerAutolaunchEnabled(bool bEnabled);
#endif

private:
	void OnEngineInitComplete();
	bool OnSettingsModified();

	void RunDefaultOSCListener();

private:
	FDelegateHandle DeferredStartDelegateHandle;
};
