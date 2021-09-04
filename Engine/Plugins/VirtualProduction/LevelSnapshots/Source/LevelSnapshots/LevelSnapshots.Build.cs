// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class LevelSnapshots : ModuleRules
{
    public LevelSnapshots(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicIncludePaths.AddRange(
	        new string[] 
	        {
		        Path.Combine(ModuleDirectory, "Public"),
		        Path.Combine(ModuleDirectory, "Public", "Data"),
		        Path.Combine(ModuleDirectory, "Public", "Restorability"),
		        Path.Combine(ModuleDirectory, "Public", "Settings")
	        }
        );
        PrivateIncludePaths.AddRange(
	        new string[] 
	        {
		        Path.Combine(ModuleDirectory, "Private"),
		        Path.Combine(ModuleDirectory, "Private", "Archive"),
		        Path.Combine(ModuleDirectory, "Private", "Data")
	        }
        );
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "TraceLog"
            }
            );


        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
	            "ApplicationCore",
	            "Core",
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "LevelSnapshotFilters"
            }
            );

		// This is needed for undo / redo system in editor
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
