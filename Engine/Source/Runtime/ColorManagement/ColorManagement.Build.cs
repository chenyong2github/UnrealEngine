// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ColorManagement : ModuleRules
{
	public ColorManagement(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Runtime/ColorManagement/Private"
			}
		);
	}
}
