// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MoviePlayer : ModuleRules
{
	public MoviePlayer(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.Add("Runtime/MoviePlayer/Private");

		PublicDependencyModuleNames.AddRange(
			new string[] {
					"Engine",
					"ApplicationCore",
				}
		);

		PrivateDependencyModuleNames.AddRange(
            new string[] {
                    "Core",
                    "InputCore",
                    "RenderCore",
                    "CoreUObject",
					"HTTP",
					"MoviePlayerProxy",
                    "RHI",
                    "Slate",
					"SlateCore",
                    "HeadMountedDisplay"
				}
        );
	}
}
