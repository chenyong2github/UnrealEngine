// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SwitchboardListener : ModuleRules
{
	public SwitchboardListener(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"ApplicationCore", // for LaunchEngineLoop.cpp dependency
				"Core",
				"CoreUObject",
				"Json",
				"Networking",
				"Projects", // for LaunchEngineLoop.cpp dependency
				"JsonUtilities",
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64)
        {
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"NVAPI",
				}
			);
		}

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Launch",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/Launch/Private", // for LaunchEngineLoop.cpp include
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"Sockets",
			}
		);
	}
}
