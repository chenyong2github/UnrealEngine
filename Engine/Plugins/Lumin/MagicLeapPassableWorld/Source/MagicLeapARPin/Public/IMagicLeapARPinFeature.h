// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Modules/ModuleManager.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"
#include "MagicLeapARPinTypes.h"

/**
 * Magic Leap AR Pin interface
 *
 * NOTE:  This intentionally does NOT derive from IModuleInterface, to allow for a cleaner separation of code if some modular interface needs to implement ARPins as well.
 * NOTE:  You must MANUALLY call IModularFeatures::Get().RegisterModularFeature( GetModularFeatureName(), this ) in your implementation!  This allows AR Pins
 *			to be both piggy-backed off modules which support them, as well as standing alone.
 */
class MAGICLEAPARPIN_API IMagicLeapARPinFeature : public IModularFeature
{
public:
	virtual ~IMagicLeapARPinFeature() {}

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static FName GetModularFeatureName()
	{
		static FName FeatureName = FName(TEXT("MagicLeapARPinFeature"));
		return FeatureName;
	}

	static inline IMagicLeapARPinFeature* Get()
	{
		TArray<IMagicLeapARPinFeature*> ARPinImpls = IModularFeatures::Get().GetModularFeatureImplementations<IMagicLeapARPinFeature>(GetModularFeatureName());
		// return the first impl for now
		return (ARPinImpls.Num() > 0) ? ARPinImpls[0] : nullptr;
	}

	/**
	* Create an ARPin tracker.
	* @return Error code representing specific success or failure cases. If code is EMagicLeapPassableWorldError::PrivilegeRequestPending
	* or EMagicLeapPassableWorldError::StartupPending, poll for IsTrackerValid() to check when the privilege is granted and tracker successfully created.
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
	* Returns the position & orientation of the requested Pin in tracking space
	* @param PinID ID of the Pin to get the position and orientation for.
	* @param Position Output param for the position of the Pin in tracking space. Valid only if return value is true.
	* @param Orientation Output param for the orientation of the Pin in tracking space. Valid only if return value is true.
	* @param PinFoundInEnvironment Output param for indicating if the requested Pin was found user's current environment or not.
	* @return true if the PinID was valid and the position & orientation were successfully retrieved.
	*/
	virtual bool GetARPinPositionAndOrientation_TrackingSpace(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) = 0;

	/**
	* Returns the world position & orientation of the requested Pin.
	* @param PinID ID of the Pin to get the position and orientation for.
	* @param Position Output param for the world position of the Pin. Valid only if return value is true.
	* @param Orientation Output param for the world orientation of the Pin. Valid only if return value is true.
	* @param PinFoundInEnvironment Output param for indicating if the requested Pin was found user's current environment or not.
	* @return true if the PinID was valid and the position & orientation were successfully retrieved.
	*/
	virtual bool GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment) = 0;

	/**
	* Returns the state of the requested Pin.
	* @param PinID ID of the Pin to get the state for.
	* @param State Output state of the Pin. Valid only if return value is true.
	* @return Error code representing specific success or failure cases.
	*/
	virtual EMagicLeapPassableWorldError GetARPinState(const FGuid& PinID, FMagicLeapARPinState& State) = 0;

	/**
	 * Delegate event to report updates in ARPins
	 * @param Added List of ARPin IDs that were added
	 * @param Updated List of ARPin IDs that were updated. Whether a pin is considered updated is determined by whehter any of its state parameters changed a specified delta.
	 * 				  The delta thresholds can be set in Project Settings > MagicLeapARPin Plugin
	 * @param Deleted List of ARPin IDs deleted
	 */
	DECLARE_EVENT_ThreeParams(IMagicLeapARPinFeature, FMagicLeapARPinUpdatedEvent, const TArray<FGuid>& /* Added */, const TArray<FGuid>& /* Updated */, const TArray<FGuid>& /* Deleted */)

	/**
	 * Getter for the OnMagicLeapARPinUpdated event, should be used to bind and unbind delegated.
	 * @return delegate event to bind to
	 */
	FMagicLeapARPinUpdatedEvent& OnMagicLeapARPinUpdated() { return OnMagicLeapARPinUpdatedEvent; }

	/**
	 * Bind a dynamic delegate to the OnMagicLeapARPinUpdated event.
	 * @param Delegate Delegate to bind
	 */
	void BindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate) { OnMagicLeapARPinUpdatedMulti.Add(Delegate); }

	/**
	 * Unbind a dynamic delegate from the OnMagicLeapARPinUpdated event.
	 * @param Delegate Delegate to unbind
	 */
	void UnBindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate) { OnMagicLeapARPinUpdatedMulti.Remove(Delegate); }

protected:
	void BroadcastOnMagicLeapARPinUpdatedEvent(const TArray<FGuid>& Added, const TArray<FGuid>& Updated, const TArray<FGuid>& Deleted)
	{
		OnMagicLeapARPinUpdatedEvent.Broadcast(Added, Updated, Deleted);
	}

	FMagicLeapARPinUpdatedEvent OnMagicLeapARPinUpdatedEvent;
	FMagicLeapARPinUpdatedMultiDelegate OnMagicLeapARPinUpdatedMulti;
};

DECLARE_LOG_CATEGORY_EXTERN(LogMagicLeapARPin, Verbose, All);
