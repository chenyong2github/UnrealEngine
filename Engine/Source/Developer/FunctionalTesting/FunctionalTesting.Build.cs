// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class FunctionalTesting : ModuleRules
{
    public FunctionalTesting(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "CoreUObject",
                "Engine",
                "RenderCore",
                "Slate",
				"SlateCore",
                "MessageLog",
                "NavigationSystem",
                "AIModule",
                "RenderCore",
                "AssetRegistry",
                "RHI",
                "UMG",
				"AutomationController",
            }
        );

        if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"SourceControl",
					"UnrealEd",
					"LevelEditor"
				}
			);
		}

        PrivateIncludePaths.AddRange(
            new string[]
            {
                "Developer/FunctionalTesting/Private",
            }
        );

		//make sure this is compiled for binary builds
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrecompileForTargets = PrecompileTargetsType.Any;
		}
	}
}
