// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "MagicLeapPlanesTypes.h"

	/**
	 * The public interface to this module.  In most cases, this interface is only public to sibling modules
	 * within this plugin.
	 */
class IMagicLeapPlanesModule : public IModuleInterface
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapPlanesModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapPlanesModule>("MagicLeapPlanes");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapPlanes");
	}

	/** Create a planes tracker. */
	virtual bool CreateTracker() = 0;

	/** Destroy a planes tracker. */
	virtual bool DestroyTracker() = 0;

	/** Is a planes tracker already created. */
	virtual bool IsTrackerValid() const = 0;

	/** Initiates a plane query with a static delegate. */
	virtual bool QueryBeginAsync(const FMagicLeapPlanesQuery& Query, const FMagicLeapPlanesResultStaticDelegate& ResultDelegate) = 0;

	/** Initiates a plane query with a dynamic delegate. */
	virtual bool QueryBeginAsync(const FMagicLeapPlanesQuery& QueryParams, const FMagicLeapPlanesResultDelegateMulti& ResultDelegate) = 0;
};
