// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "UObject/ObjectMacros.h"
#if PLATFORM_LUMIN
#include "Lumin/LuminPlatformFile.h"
#endif // PLATFORM_LUMIN

UENUM(BlueprintType)
enum class EMagicLeapDispatchResult : uint8
{
	Ok,
	CannotStartApp,
	InvalidPacket,
	NoAppFound,
	AppPickerDialogFailure,
	AllocFailed,
	InvalidParam,
	UnspecifiedFailure,
	NotImplemented
};

/**
 * The public interface to this module.  In most cases, this interface is only public to sibling modules
 * within this plugin.
 */
class MAGICLEAPDISPATCH_API IMagicLeapDispatchPlugin : public IModuleInterface
{
public:

	/**
	 * Singleton-like access to this module's interface.  This is just for convenience!
	 * Beware of calling this during the shutdown phase, though.  Your module might have been unloaded already.
	 *
	 * @return Returns singleton instance, loading the module on demand if needed
	 */
	static inline IMagicLeapDispatchPlugin& Get()
	{
		return FModuleManager::LoadModuleChecked<IMagicLeapDispatchPlugin>("MagicLeapDispatch");
	}

	/**
	 * Checks to see if this module is loaded and ready.  It is only valid to call Get() if IsAvailable() returns true.
	 *
	 * @return True if the module is loaded and ready to use
	 */
	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MagicLeapDispatch");
	}

#if PLATFORM_LUMIN
	/**
	 * Attempt to launch another app on the Magic Leap device that can handle the mime types of file extensions of 
	 * the files provided as arguments. These files will be sent to the target app as init args.
	 * @param DispatchFileList A list of file info objects to be dispatched to another app. File info objects contain a file's name, mime type (optional) and it's IFileHandle pointer.
	 * @return the dispatch result
	 */
	virtual EMagicLeapDispatchResult TryOpenApplication(const TArray<FLuminFileInfo>& DispatchFileList) = 0;
#endif // PLATFORM_LUMIN
};
