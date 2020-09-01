// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Delegates/IDelegateInstance.h"

class FAssetTypeActions_ChaosCacheCollection;

class UToolMenu;
struct FToolMenuSection;

/**
 * The public interface to this module
 */
class IChaosCachingEditorPlugin : public IModuleInterface
{
	TArray<IConsoleObject*> EditorCommands;

public:
	virtual void StartupModule();
	virtual void ShutdownModule();

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IChaosCachingEditorPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<IChaosCachingEditorPlugin>("ChaosCachingEditorPlugin");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable() { return FModuleManager::Get().IsModuleLoaded("ChaosCachingEditorPlugin"); }

private:

	void RegisterMenus();
	void RegisterCachingSubMenu(UToolMenu* InMenu, FToolMenuSection* InSection);
	
	void OnCreateCacheManager();
	void OnSetAllPlay();
	void OnSetAllRecord();

	FAssetTypeActions_ChaosCacheCollection* AssetTypeActions_ChaosCacheCollection;
	FDelegateHandle StartupHandle;
};
