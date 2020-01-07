// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "IHeadMountedDisplayModule.h"
#include "Stats/Stats.h"
#include "Lumin/CAPIShims/LuminAPICoordinateFrameUID.h"

class IMagicLeapTrackerEntity;

DECLARE_STATS_GROUP(TEXT("MagicLeap"), STATGROUP_MagicLeap, STATCAT_Advanced);

enum class EMagicLeapTransformFailReason : uint8
{
	None,
	InvalidTrackingFrame,
	NaNsInTransform,
	CallFailed,
	PoseNotFound,
	HMDNotInitialized
};

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class IMagicLeapPlugin : public IHeadMountedDisplayModule
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked< IMagicLeapPlugin >("MagicLeap");
	}

	/**
	 * Singleton-like access to this module's interface.
	 * Checks whether the module is loaded, asserting on failure.
	 *
	 * @return Returns singleton instance
	 */
	static inline IMagicLeapPlugin& GetModuleChecked()
	{
		return FModuleManager::GetModuleChecked< IMagicLeapPlugin >("MagicLeap");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeap");
	}

	/**
	 * Checks to see if the XRSystem instance is a MagicLeap HMD.
	 *
	 * @return True if the current XRSystem instance is a MagicLeap HMD device
	 */
	virtual bool IsMagicLeapHMDValid() const = 0;

	/**
	 * Register an IMagicLeapTrackerEntity with the HMD to receive callbacks for tracker creation, desctruction and late update.
	 * @param TrackerEntity Pointer to the object that implements the IMagicLeapTrackerEntity interface.
	 */
	virtual void RegisterMagicLeapTrackerEntity(IMagicLeapTrackerEntity* TrackerEntity) = 0;

	/**
	 * Unregister an IMagicLeapTrackerEntity with the HMD. Object will no longer receive callbacks for tracker creation, desctruction and late update.
	 * @param TrackerEntity Pointer to the object that implements the IMagicLeapTrackerEntity interface.
	 */
	virtual void UnregisterMagicLeapTrackerEntity(IMagicLeapTrackerEntity* TrackerEntity) = 0;

	/**
	 * Enable/Disable mouse and keyboard input when playing in Editor.
	 * @param Ignore If true, mouse and keyboard input is ignored by the viewport. If false, the input enabled again.
	 * @return Whether input is being ignored now or not.
	 */
	virtual bool SetIgnoreInput(bool Ignore) = 0;

	/**
	 * Get latest transform for a given CoordinateFrameUID in Tracking space. Transform to World space using UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform().
	 * @param Id The CoordinateFrameUID, as an FGuid, to get the transform for.
	 * @param OutTransform Transform of the given CoordinateFrameUID.
	 * @param OutReason Reason for this function's failure.
	 * @return true if OutTransform is populated with a valid value. If false, the reason for failure is populated in OutReason.
	 */
	virtual bool GetTransform(const FGuid& Id, FTransform& OutTransform, EMagicLeapTransformFailReason& OutReason) const = 0;

#if WITH_MLSDK
	/**
	 * Get latest transform for a given CoordinateFrameUID in Tracking space. Transform to World space using UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform().
	 * @param Id The CoordinateFrameUID to get the transform for.
	 * @param OutTransform Transform of the given CoordinateFrameUID.
	 * @param OutReason Reason for this function's failure.
	 * @return true if OutTransform is populated with a valid value. If false, the reason for failure is populated in OutReason.
	 */
	virtual bool GetTransform(const MLCoordinateFrameUID& Id, FTransform& OutTransform, EMagicLeapTransformFailReason& OutReason) const = 0;
#endif // WITH_MLSDK

	/**
	 * Scale of 1uu to 1m in real world measurements.
	 * @return Default value is 100.0f i.e. 1uu = 100cm.
	 */
	virtual float GetWorldToMetersScale() const = 0;

	/**
	 * Check if the perception system has been started.
	 * @return true if perception system has been started, false otherwise.
	 */
	virtual bool IsPerceptionEnabled() const = 0;

	/**
	 * Check if MLAudio mixer should be used when running with MLRemote.
	 * @return true MLAudio mixer should be used, false otherwise.
	 */
	virtual bool UseMLAudioForZI() const = 0;
};

class IARSystemSupport;
class FXRTrackingSystemBase;

/**
 * The public interface to this module.
 */
class ILuminARModule : public IModuleInterface
{
public:
	//create for mutual connection (regardless of construction order)
	virtual TSharedPtr<IARSystemSupport, ESPMode::ThreadSafe> CreateARImplementation() = 0;
	//Now connect (regardless of connection order)
	virtual void ConnectARImplementationToXRSystem(FXRTrackingSystemBase* InXRTrackingSystem) = 0;
	//Now initialize fully connected systems
	virtual void InitializeARImplementation() = 0;
};
