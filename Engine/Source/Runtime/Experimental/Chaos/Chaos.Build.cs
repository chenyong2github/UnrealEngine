// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class Chaos : ModuleRules
    {
        public Chaos(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePaths.Add("Runtime/Experimental/Chaos/Public");

            PublicDependencyModuleNames.AddRange(
                new string[] {
                "Core",
                "CoreUObject",
				"ChaosCore",
                "IntelISPC",
                "Voronoi"
                }
            );

            PublicDefinitions.Add("COMPILE_WITHOUT_UNREAL_SUPPORT=0");

            if (Target.bCompileChaos == true || Target.bUseChaos == true)
            {
                PublicDefinitions.Add("INCLUDE_CHAOS=1");
            }
            else
            {
                PublicDefinitions.Add("INCLUDE_CHAOS=0");
            }

			if (Target.bUseChaosMemoryTracking == true)
			{
				PublicDefinitions.Add("CHAOS_MEMORY_TRACKING=1");
			}
			else
			{
				PublicDefinitions.Add("CHAOS_MEMORY_TRACKING=0");
			}
        }
    }
}
