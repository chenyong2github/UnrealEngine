// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================================
	AndroidFile.h: Android platform File functions
==============================================================================================*/

#pragma once

#include "GenericPlatform/GenericPlatformFile.h"
#if USE_ANDROID_JNI
#include <jni.h>
#endif

#if PLATFORM_USE_PLATFORM_FILE_MANAGED_STORAGE_WRAPPER
#include "HAL/IPlatformFileManagedStorageWrapper.h"
#endif //PLATFORM_USE_PLATFORM_FILE_MANAGED_STORAGE_WRAPPER

/**
	Android File I/O implementation with additional utilities to deal
	with Java side access.
**/
class CORE_API IAndroidPlatformFile : public IPhysicalPlatformFile
{
public:
	static IAndroidPlatformFile & GetPlatformPhysical();

#if USE_ANDROID_FILE
	/**
	 * Get the directory path to write log files to.
	 * This is /temp0 in shipping, or a path inside /data for other configs.
	 */
	static const FString* GetOverrideLogDirectory();
#endif

#if USE_ANDROID_JNI
	// Get the android.content.res.AssetManager that Java code
	// should use to open APK assets.
	virtual jobject GetAssetManager() = 0;
#endif

	// Get detailed information for a file that
	// we can hand to other Android media classes for access.

	// Is file embedded as an asset in the APK?
	virtual bool IsAsset(const TCHAR* Filename) = 0;

	// Offset within file or asset where its data starts.
	// Note, offsets for assets is relative to complete APK file
	// and matches what is returned by AssetFileDescriptor.getStartOffset().
	virtual int64 FileStartOffset(const TCHAR* Filename) = 0;

	// Get the root, i.e. underlying, path for the file. This
	// can be any of: a resolved file path, an OBB path, an
	// asset path.
	virtual FString FileRootPath(const TCHAR* Filename) = 0;

	virtual FString ConvertToAbsolutePathForExternalAppForRead(const TCHAR* Filename) override;
	virtual FString ConvertToAbsolutePathForExternalAppForWrite(const TCHAR* Filename) override;

#if PLATFORM_USE_PLATFORM_FILE_MANAGED_STORAGE_WRAPPER
	//Special Initialize to handle the very early initialize needed by Android to pass it into the underlying FAndroidPlatformFile 
	//layer instead of trying to handle it on the PerstentStorageManager
	void EarlyInitializeForStorageWrapper(const TCHAR* CommandLineParam);

	static FManagedStoragePlatformFile& GetManagedStorageWrapper();
#endif // PLATFORM_USE_PLATFORM_FILE_MANAGED_STORAGE_WRAPPER
};
