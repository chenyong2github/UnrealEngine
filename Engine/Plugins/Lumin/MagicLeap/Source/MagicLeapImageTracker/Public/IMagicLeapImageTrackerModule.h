// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "MagicLeapImageTrackerTypes.h"

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class IMagicLeapImageTrackerModule : public IModuleInterface
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapImageTrackerModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapImageTrackerModule>("MagicLeapImageTracker");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapImageTracker");
	}

	virtual bool GetImageTrackerEnabled() const = 0;
	virtual void SetImageTrackerEnabled(bool bEnabled) = 0;
	virtual void SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetCompletedStaticDelegate& SucceededDelegate, const FMagicLeapSetImageTargetCompletedStaticDelegate& FailedDelegate) = 0;
	virtual void SetTargetAsync(const FMagicLeapImageTargetSettings& ImageTarget, const FMagicLeapSetImageTargetSucceededMulti& SucceededDelegate, const FMagicLeapSetImageTargetFailedMulti& FailedDelegate) = 0;
	virtual bool RemoveTargetAsync(const FString& TargetName) = 0;
	virtual void DestroyTracker() = 0;
	virtual void GetTargetState(const FString& TargetName, bool bProvideTransformInTrackingSpace, FMagicLeapImageTargetState& TargetState) const = 0;
	virtual FGuid GetTargetHandle(const FString& TargetName) const = 0;
};
