// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleManager.h"
#include "MagicLeapARPinTypes.h"

	/**
	 * The public interface to this module.  In most cases, this interface is only public to sibling modules
	 * within this plugin.
	 */
class MAGICLEAPARPIN_API IMagicLeapARPinModule : public IModuleInterface
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapARPinModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapARPinModule>("MagicLeapARPin");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapARPin");
	}

	/**
	* Create an ARPin tracker.
	* @return Error code representing specific success or failure cases. If code is EMagicLeapPassableWorldError::PrivilegeRequestPending,
	* poll for IsTrackerValid() to check when the privilege is granted and tracker successfully created.
	*/
	virtual EMagicLeapPassableWorldError CreateTracker() = 0;

	/**
	* Destroy an ARPin tracker.
	* @return Error code representing specific success or failure cases.,
	*/
	virtual EMagicLeapPassableWorldError DestroyTracker() = 0;

	/** Is an ARPin tracker already created. */
	virtual bool IsTrackerValid() const = 0;

	/**
	* Returns the count of currently available AR Pins.
	* @param Count Output param for number of currently available AR Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetNumAvailableARPins(int32& Count) = 0;

	/**
	* Returns all the AR Pins currently available.
	* @param NumRequested Max number of AR Pins to query. Pass in a negative integer to get all available Pins.
	* @param Pins Output array containing IDs of the found Pins. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetAvailableARPins(int32 NumRequested, TArray<FGuid>& Pins) = 0;

	/**
	* Returns the Pin closest to the target point passed in.
	* @param SearchPoint Position, in world space, to search the closest Pin to.
	* @param PinID Output param for the ID of the closest Pin. Valid only if return value is EMagicLeapPassableWorldError::None.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetClosestARPin(const FVector& SearchPoint, FGuid& PinID) = 0;

	/**
	* Returns the world position & orientation of the requested Pin.
	* @param PinID ID of the Pin to get the position and orientation for.
	* @param Position Output param for the world position of the Pin. Valid only if return value is true.
	* @param Orientation Output param for the world orientation of the Pin. Valid only if return value is true.
	* @param PinFoundInEnvironment Output param for indicating if the requested Pin was found user's current environment or not.
	* @return true if the PinID was valid and the position & orientation were successfully retrieved.
	*/
	virtual bool GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) = 0;
};
