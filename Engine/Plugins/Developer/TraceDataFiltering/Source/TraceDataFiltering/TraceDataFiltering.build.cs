// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TraceDataFiltering : ModuleRules
{
	public TraceDataFiltering(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "Core",
					"CoreUObject",
                    "SlateCore",
                    "Slate",
                    "InputCore",
					"EditorStyle",
                    "TraceLog",
					"TraceAnalysis",
					"TraceServices",
                    "TraceInsights",
					"Sockets"
                }
            );

		if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("SharedSettingsWidgets");
        }
    }
}
