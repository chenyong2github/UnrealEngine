// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StaticArray.h"

class FCompositeBuffer;
class FCbPackage;

#ifndef UE_WITH_ZEN
#	if PLATFORM_WINDOWS
#		define UE_WITH_ZEN 1
#	else
#		define UE_WITH_ZEN 0
#	endif
#endif

#if UE_WITH_ZEN

namespace UE::Zen {

class FZenServiceInstance
{
public:
	 DERIVEDDATACACHE_API FZenServiceInstance();
	 DERIVEDDATACACHE_API ~FZenServiceInstance();

	 DERIVEDDATACACHE_API bool IsServiceRunning();
	 DERIVEDDATACACHE_API bool IsServiceReady();

private:
};

} // namespace UE::Zen

#endif // UE_WITH_ZEN
