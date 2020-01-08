// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class BlastCore : ModuleRules
	{
        public BlastCore(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Blast"

                }
            );
        }
    }
}
