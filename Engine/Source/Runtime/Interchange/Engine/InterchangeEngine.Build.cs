// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class InterchangeEngine : ModuleRules
{
	public InterchangeEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"InterchangeCore"
				}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"Engine",
			}
		);
	}
}
