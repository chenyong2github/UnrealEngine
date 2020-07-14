// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionEngine : ModuleRules
	{
        public GeometryCollectionEngine(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("Runtime/Experimental/GeometryCollectionEngine/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "RenderCore",
                    "RHI",
					"Renderer",
                    "Chaos",
					"ChaosSolvers",
                    "PhysX",
                    "FieldSystemCore",
                    "FieldSystemEngine",
                    "GeometryCollectionCore", 
                    "GeometryCollectionSimulationCore",
	                "ChaosSolverEngine",
                    "IntelISPC",
					"PhysicsSQ"
                }
                );

			if (Target.bCompileAPEX)
			{
				PublicDependencyModuleNames.Add("APEX");
			}

	        if (!Target.bBuildRequiresCookedData)
			{
				DynamicallyLoadedModuleNames.Add("DerivedDataCache");
			}

			PrivateIncludePathModuleNames.Add("DerivedDataCache");

			if (Target.bBuildEditor)
            {
				DynamicallyLoadedModuleNames.Add("NaniteBuilder");
				PrivateIncludePathModuleNames.Add("NaniteBuilder");

				PrivateDependencyModuleNames.Add("UnrealEd");
			}

			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}
}
