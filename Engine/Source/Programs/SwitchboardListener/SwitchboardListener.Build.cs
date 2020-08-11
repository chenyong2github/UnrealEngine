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
				"PerforceSourceControl",
				"Projects", // for LaunchEngineLoop.cpp dependency
				"SourceControl",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Launch",
				"SourceControl",
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
