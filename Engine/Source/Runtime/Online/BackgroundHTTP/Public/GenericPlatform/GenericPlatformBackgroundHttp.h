// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"

#include "Interfaces/IBackgroundHttpRequest.h"

class IBackgroundHttpManager;

/**
 * Generic version of Background Http implementations for platforms that don't need a special implementation
 * Intended usage is to use FPlatformBackgroundHttp instead of FGenericPlatformHttp
 * On platforms without a specific implementation, you should still use FPlatformBackgroundHttp and it will call into these functions
 */
class BACKGROUNDHTTP_API FGenericPlatformBackgroundHttp
{
public:
	/**
	 * Platform initialization step
	 */
	static void Initialize();

	/**
	 * Platform shutdown step
	 */
	static void Shutdown();

	/**
	 * Creates a platform-specific Background HTTP manager.
	 * Un-implemented platforms should create a FGenericPlatformBackgroundHttpManager
	 */
	static FBackgroundHttpManagerPtr CreatePlatformBackgroundHttpManager();

	/**
	 * Creates a new Background Http request instance for the current platform
	 * that will continue to download when the application is in the background
	 *
	 * @return request object
	 */
	static FBackgroundHttpRequestPtr ConstructBackgroundRequest();

	/**
	 * Creates a new Background Http Response instance for the current platform
	 * This normally is called by the request and associated with itself.
	 *
	 * @return response object
	 */
	static FBackgroundHttpResponsePtr ConstructBackgroundResponse(int32 ResponseCode, const FString& TempFilePath);

	/**
	* Function that takes in a URL and figures out the location we should use as the temp storage URL
	*
	* @param URL FString URL of the request that we need to find the end location for.
	*
	* @return FString File Path the temp file would be located at for the supplied URL.
	*/
	static const FString GetTemporaryFilePathFromURL(const FString& URL);

	/**
	* Function that returns the root path where all our temporary files are stored on this platform
	*
	* @return FString File path that is the root path of our temp files for background http work on this platform.
	*/
	static const FString& GetTemporaryRootPath();
};
