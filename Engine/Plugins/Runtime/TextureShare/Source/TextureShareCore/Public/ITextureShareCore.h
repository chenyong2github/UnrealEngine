// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "ITextureShareItem.h"
#include "ITextureShareItemD3D11.h"
#include "ITextureShareItemD3D12.h"

class ITextureShareCore
	: public IModuleInterface
{
	static constexpr auto ModuleName = TEXT("TextureShareCore");

	public:
	/**
		* Singleton-like access to this module's interface.  This is just for convenience!
		* Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
		*
		* @return Returns singleton instance, loading the module on demand if needed
		*/
	static inline ITextureShareCore& Get()
	{
		return FModuleManager::LoadModuleChecked<ITextureShareCore>(ITextureShareCore::ModuleName);
	}

	/**
		* Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
		*
		* @return True if the module is loaded and ready to use
		*/
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(ITextureShareCore::ModuleName);
	}

	/**
	 * Get global synchronization settings
	 *
	 * @param Process        - Process logic type: server or client
	 *
	 * @return settings data struct
	 */
	virtual FTextureShareSyncPolicySettings GetSyncPolicySettings(ETextureShareProcess Process) const = 0;

	/**
	 * Get global synchronization settings
	 *
	 * @param Process              - Process logic type: server or client
	 * @param InSyncPolicySettings - settings data struct
	 */
	virtual void SetSyncPolicySettings(ETextureShareProcess Process, const FTextureShareSyncPolicySettings& InSyncPolicySettings) = 0;

	/**
	 * Create ITextureShareItem object
	 *
	 * @param ShareName      - Unique share name (case insensitive)
	 * @param Process        - Process logic type: server or client
	 * @param SyncMode       - Synchronization settings
	 * @param DeviceType     - Render device type
	 * @param OutShareObject - Return object ptr
	 *
	 * @return True if the success
	 */
	virtual bool CreateTextureShareItem(const FString& ShareName, ETextureShareProcess Process, FTextureShareSyncPolicy SyncMode, ETextureShareDevice DeviceType, TSharedPtr<ITextureShareItem>& OutShareObject, float SyncWaitTime) = 0;

	/**
	 * Delete ITextureShareItem object
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 *
	 * @return True if the success
	 */
	virtual bool ReleaseTextureShareItem(const FString& ShareName) = 0;

	/**
	 * Get ITextureShareItem low-level api object
	 *
	 * @param ShareName - Unique share name (case insensitive)
	 *
	 * @return sharedPtr to TextureShareItem object
	 */
	virtual bool GetTextureShareItem(const FString& ShareName, TSharedPtr<ITextureShareItem>& OutShareObject) const = 0;

	/**
	 * Release all created ITextureShareItem objects
	 */
	virtual void ReleaseLib() = 0;

	/** Frame sync scope */

	/**
	 * Begin global frame sync
	 * NOT IMPLEMENTED
	 */
	virtual bool BeginSyncFrame() = 0;

	/**
	 * Finalize global frame sync
	 * NOT IMPLEMENTED
	 */
	virtual bool EndSyncFrame() = 0;
};
