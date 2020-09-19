// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Rendering/StaticLightingSystemInterface.h"


/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules 
 * within this plugin.
 */
class IGPULightmassModule : public IModuleInterface
{

public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IGPULightmassModule& Get()
	{
		return FModuleManager::LoadModuleChecked< IGPULightmassModule >( "GPULightmass" );
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded( "GPULightmass" );
	}
};

class FGPULightmassModule : public IGPULightmassModule, public IStaticLightingSystemImpl
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual bool SupportsRealtimePreview() override { return true; }

	virtual IStaticLightingSystem* AllocateStaticLightingSystemForWorld(UWorld* InWorld) override;
	virtual IStaticLightingSystem* AllocateStaticLightingSystemForWorldWithSettings(UWorld* InWorld, class UGPULightmassSettings* Settings);
	virtual void RemoveStaticLightingSystemForWorld(UWorld* InWorld) override;
	virtual IStaticLightingSystem* GetStaticLightingSystemForWorld(UWorld* InWorld) override;
	virtual void EditorTick() override;
	virtual bool IsStaticLightingSystemRunning() override;

	// Due to limitations in our TMap implementation I cannot use TUniquePtr here
	// But the GPULightmassModule is the only owner of all static lighting systems, and all worlds weak refer to the systems
	TMap<UWorld*, class FGPULightmass*> StaticLightingSystems;

	FSimpleMulticastDelegate OnStaticLightingSystemsChanged;
};

DECLARE_LOG_CATEGORY_EXTERN(LogGPULightmass, Log, All);
