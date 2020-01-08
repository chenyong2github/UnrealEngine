// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "MagicLeapPrivilegeTypes.h"

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class IMagicLeapPrivilegesModule : public IModuleInterface
{
public:
	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapPrivilegesModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapPrivilegesModule>("MagicLeapPrivileges");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapPrivileges");
	}

	/**
	 * Requests the given privilege asynchronously.
	 *
	 * @param ResultDelegate The static delegate to be notified upon completion of the privilege request.
	 * @return True if the request was initiated successfully, false otherwise.
	 */
	virtual bool RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestStaticDelegate& ResultDelegate) = 0;
	
	/**
	 * Requests the given privilege asynchronously.
	 *
	 * @param ResultDelegate The dynamic delegate to be notified upon completion of the privilege request.
	 * @return True if the request was initiated successfully, false otherwise.
	 */
	virtual bool RequestPrivilegeAsync(EMagicLeapPrivilege Privilege, const FMagicLeapPrivilegeRequestDelegate& ResultDelegate) = 0;
};
