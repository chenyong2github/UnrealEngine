// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class HoloLensDeviceDetector : ModuleRules
{
	public HoloLensDeviceDetector(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"TargetPlatform",
				"HoloLensTargetPlatform",
				"DesktopPlatform",
				"HTTP",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"HoloLensTargetPlatform/Private"
			}
		);

		if (Target.WindowsPlatform.bUseWindowsSDK10)
		{
			bEnableExceptions = true;
			PCHUsage = PCHUsageMode.NoSharedPCHs;
			PrivatePCHHeaderFile = "Public/IHoloLensDeviceDetectorModule.h";
            PublicDefinitions.Add("USE_WINRT_DEVICE_WATCHER=1");
		}
		else
		{
            PublicDefinitions.Add("USE_WINRT_DEVICE_WATCHER=0");
		}
	}
}