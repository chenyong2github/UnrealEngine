// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

struct FAnalyticsEventAttribute;

class IUsdClassesModule : public IModuleInterface
{
public:
	/** Updates all plugInfo.json to point their LibraryPaths to TargetDllFolder */
	USDCLASSES_API static void UpdatePlugInfoFiles( const FString& PluginDirectory, const FString& TargetDllFolder );

	/**
	 * Sends analytics about a USD operation
	 * @param InAttributes - Additional analytics events attributes to send, along with new ones collected within this function
	 * @param EventName - Name of the analytics event (e.g. "Export.StaticMesh", so that the full event name is "Engine.Usage.USD.Export.StaticMesh")
	 * @param bAutomated - If the operation was automated (e.g. came from a Python script)
	 * @param ElapsedSeconds - How long the operation took in seconds
	 * @param NumberOfFrames - Number of time codes in the exported/imported/opened stage
	 * @param Extension - Extension of the main USD file opened/emitted/imported (e.g. "usda" or "usd")
	 */
	USDCLASSES_API static void SendAnalytics( TArray<FAnalyticsEventAttribute>&& InAttributes, const FString& EventName, bool bAutomated, double ElapsedSeconds, double NumberOfFrames, const FString& Extension );
};