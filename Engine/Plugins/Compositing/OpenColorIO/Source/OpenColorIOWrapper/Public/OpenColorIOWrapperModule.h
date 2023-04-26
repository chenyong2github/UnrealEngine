// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"

class FOpenColorIOConfigWrapper;

DECLARE_LOG_CATEGORY_EXTERN(LogOpenColorIOWrapper, Log, All);

/**
 * Interface for the OpenColorIO module.
 */
class IOpenColorIOWrapperModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static IOpenColorIOWrapperModule& Get()
	{
		static const FName ModuleName = "OpenColorIOWrapper";
		return FModuleManager::LoadModuleChecked<IOpenColorIOWrapperModule>(ModuleName);
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() during shutdown if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static bool IsAvailable()
	{
		static const FName ModuleName = "OpenColorIOWrapper";
		return FModuleManager::Get().IsModuleLoaded(ModuleName);
	}

	/**
	 * Returns a minimal dynamically-created native config for conversions between interchange and working color spaces.
	 */
	virtual const FOpenColorIOConfigWrapper* GetWorkingColorSpaceToInterchangeConfig() const = 0;

	/**
	* Load a globally-shared config in the module.
	*
	* @param InFilePath Config absolute file path.
	*/
	virtual void LoadGlobalConfig(FStringView InFilePath) = 0;

	/**
	* Returns the globally-shared module config if loaded, nullptr otherwise.
	*/
	virtual const FOpenColorIOConfigWrapper* GetGlobalConfig() const = 0;

	/** Virtual destructor */
	virtual ~IOpenColorIOWrapperModule() = default;
};
