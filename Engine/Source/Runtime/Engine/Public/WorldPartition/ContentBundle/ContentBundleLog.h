// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

ENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogContentBundle, Log, All);

class FContentBundleBase;	
class FContentBundleClient;
class FContentBundleContainer;
class UContentBundleDescriptor;
class UWorld;

namespace ContentBundle
{
	namespace Log
	{
#if !NO_LOGGING
		FString MakeDebugInfoString(const FContentBundleBase& ContentBundle);
		FString MakeDebugInfoString(const FContentBundleClient& ContentBundleClient);
		FString MakeDebugInfoString(const FContentBundleContainer& ContentBundleContainer);
		FString MakeDebugInfoString(const FContentBundleClient& ContentBundleClient, UWorld* World);
#endif
	}
}