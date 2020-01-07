// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "MagicLeapLightEstimationTypes.h"

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class MAGICLEAPLIGHTESTIMATION_API IMagicLeapLightEstimationPlugin : public IModuleInterface
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapLightEstimationPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapLightEstimationPlugin>("MagicLeapLightEstimation");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapLightEstimation");
	}

	/**
		Create a light estimation tracker.
		@return true if tracker was successfully created, false otherwise
	*/
	virtual bool CreateTracker() = 0;

	/** Destroy a light estimation tracker. */
	virtual void DestroyTracker() = 0;

	/**
		Check if a light estimation tracker has already been created.
		@return true if tracker already exists and is valid, false otherwise
	*/
	virtual bool IsTrackerValid() const = 0;

	/**
		Gets information about the ambient light sensor global state.
		@note Capturing images or video will stop the lighting information update, causing the retrieved data to be stale (old timestamps).
		@param GlobalAmbientState Output param containing the information about the global lighting state (ambient intensity). Valid only if return value of function is true.
		@return true if the global ambient state was succesfully retrieved.
	*/
	virtual bool GetAmbientGlobalState(FMagicLeapLightEstimationAmbientGlobalState& GlobalAmbientState) const = 0;

	/**
		Gets information about the color temperature state.
		@note Capturing images or video will stop the lighting information update, causing the retrieved data to be stale (old timestamps).
		@param ColorTemperatureState Output param containing the information about the color temperature. Valid only if return value of function is true.
		@return true if the color temperature state was succesfully retrieved.
	*/
	virtual bool GetColorTemperatureState(FMagicLeapLightEstimationColorTemperatureState& ColorTemperatureState) const = 0;
};
