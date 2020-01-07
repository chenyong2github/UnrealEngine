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
	            DynamicallyLoadedModuleNames.AddRange(new string[] { "DerivedDataCache" });
		    }

			PrivateIncludePathModuleNames.Add("DerivedDataCache");

			if(Target.bBuildEditor)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
            }
        }
	}
}
