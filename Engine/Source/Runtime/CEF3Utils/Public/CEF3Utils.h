// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_CEF3
namespace CEF3Utils
{
	/**
	 * Load the required modules for CEF3, returns false if we fail to load the cef library
	 */
	CEF3UTILS_API bool LoadCEF3Modules(bool bIsMainApp);

	/**
	 * Unload the required modules for CEF3
	 */
	CEF3UTILS_API void UnloadCEF3Modules();
};
#endif //WITH_CEF3
