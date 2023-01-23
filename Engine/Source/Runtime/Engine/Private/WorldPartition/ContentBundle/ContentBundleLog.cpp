// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ContentBundle/ContentBundleLog.h"

#include "Engine/World.h"
#include "WorldPartition/ContentBundle/ContentBundleBase.h"
#include "WorldPartition/ContentBundle/ContentBundleDescriptor.h"
#include "WorldPartition/ContentBundle/ContentBundleClient.h"
#include "WorldPartition/ContentBundle/ContentBundleContainer.h"
#include "UnrealEngine.h"

DEFINE_LOG_CATEGORY(LogContentBundle);

namespace ContentBundle
{
#if !NO_LOGGING

	namespace LogPrivate
	{
		FString MakeDebugInfoString(const UContentBundleDescriptor& ContentBundleDescriptor)
		{
			return FString::Printf(TEXT("[CB: %s]"), *ContentBundleDescriptor.GetDisplayName());
		}

		FString MakeWorldDebugInfoString(UWorld* World)
		{
			return FString::Printf(TEXT("[%s(%s)]"), *World->GetName(), *GetDebugStringForWorld(World));
		}

		FString MakeDebugInfoString(const UContentBundleDescriptor& ContentBundleDescriptor, UWorld* World)
		{
			return FString::Printf(TEXT("%s%s"), *MakeDebugInfoString(ContentBundleDescriptor), *MakeWorldDebugInfoString(World));
		}

		
	}

FString Log::MakeDebugInfoString(const FContentBundleBase& ContentBundle)
{
	return LogPrivate::MakeDebugInfoString(*ContentBundle.GetDescriptor(), ContentBundle.GetInjectedWorld());
}

FString Log::MakeDebugInfoString(const FContentBundleClient& ContentBundleClient)
{
	return LogPrivate::MakeDebugInfoString(*ContentBundleClient.GetDescriptor());
}

FString Log::MakeDebugInfoString(const FContentBundleContainer& ContentBundleContainer)
{
	return LogPrivate::MakeWorldDebugInfoString(ContentBundleContainer.GetInjectedWorld());
}

FString Log::MakeDebugInfoString(const FContentBundleClient& ContentBundleClient, UWorld* World)
{
	return LogPrivate::MakeDebugInfoString(*ContentBundleClient.GetDescriptor(), World);
}





#endif
}