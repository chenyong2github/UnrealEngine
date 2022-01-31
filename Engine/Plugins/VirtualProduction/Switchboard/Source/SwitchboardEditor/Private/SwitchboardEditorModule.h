// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Delegates/IDelegateInstance.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "SwitchboardScriptInterop.h"



DECLARE_LOG_CATEGORY_EXTERN(LogSwitchboardPlugin, Log, All);


namespace UE::Switchboard::Private
{
	template <typename... TPaths>
	FString ConcatPaths(FString BaseDir, TPaths... InPaths)
	{
		return (FPaths::ConvertRelativePathToFull(BaseDir) / ... / InPaths);
	}
} // namespace UE::Switchboard::Private


class FSwitchboardEditorModule : public IModuleInterface
{
public:
	static const FString& GetSbScriptsPath();
	static const FString& GetSbThirdPartyPath();

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

	bool LaunchSwitchboard();
	bool LaunchListener();

	TSharedFuture<FSwitchboardVerifyResult> GetVerifyResult(bool bForceRefresh = false);

#if SB_LISTENER_AUTOLAUNCH
	/**
	 * Returns whether (this engine's) SwitchboardListener is configured to run automatically.
	 * Defaults to returning a cached value to avoid hitting the registry.
	 */
	bool IsListenerAutolaunchEnabled(bool bForceRefreshCache = false);

	/** Enables or disables auto-run of SwitchboardListener. */
	bool SetListenerAutolaunchEnabled(bool bEnabled);
#endif

private:
	void OnEngineInitComplete();
	bool OnEditorSettingsModified();

	void RunDefaultOSCListener();

private:
#if SB_LISTENER_AUTOLAUNCH
	bool GetListenerAutolaunchEnabled_Internal() const;
#endif
	bool RunProcess(const FString& InExe, const FString& InArgs);

	FDelegateHandle DeferredStartDelegateHandle;

#if SB_LISTENER_AUTOLAUNCH
	bool bCachedAutolaunchEnabled;
#endif

	FString VerifyPath;
	TSharedFuture<FSwitchboardVerifyResult> VerifyResult;
};
