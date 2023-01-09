// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DMXControlConsoleEditor : ModuleRules
{
	public DMXControlConsoleEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				System.IO.Path.Combine(GetModuleDirectory("DMXControlConsole"), "Private"),
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{              
				"AssetTools",
				"AssetRegistry",
				"CoreUObject",
				"DMXControlConsole",
				"DMXEditor",
				"DMXProtocol",
				"DMXProtocolEditor",
				"DMXRuntime",
				"Engine",
				"InputCore",
				"LevelEditor",
				"Projects",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
			}
		);
	}
}
