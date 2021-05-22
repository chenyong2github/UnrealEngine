// Copyright Epic Games, Inc. All Rights Reserved.

using System.Linq;
using System.Collections.Generic;

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
				"TraceLog",
                "Voronoi"
                }
            );

            PublicDefinitions.Add("COMPILE_WITHOUT_UNREAL_SUPPORT=0");
			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");

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
			
			if (!Target.GlobalDefinitions.Contains("UE_LARGE_WORLD_COORDINATES_DISABLED=0")) // LWC_TODO: Temporarily disabled for LWC while these are properly fixed
			{
				UnsafeTypeCastWarningLevel = WarningLevel.Warning;
			}
		}
    }
}
