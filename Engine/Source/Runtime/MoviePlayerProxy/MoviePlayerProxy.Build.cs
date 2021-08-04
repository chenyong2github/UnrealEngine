// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MoviePlayerProxy : ModuleRules
{
	public MoviePlayerProxy(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.Add("Runtime/MoviePlayerProxy/Private");

		PrivateDependencyModuleNames.AddRange(
            new string[]
			{
                    "Core",
			}
			);
	}
}
