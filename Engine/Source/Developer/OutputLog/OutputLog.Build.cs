// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OutputLog : ModuleRules
{
	public OutputLog(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject", // @todo Mac: for some reason it's needed to link in debug on Mac
                "InputCore",
				"Slate",
				"SlateCore",
                "TargetPlatform",
                "DesktopPlatform",
				"ToolWidgets", 
				"Engine", 
			}
		);

		if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(
				new string[] {
					"EditorFramework",
					"UnrealEd",
					"StatusBar",
				}
			);
        }
    }
}
