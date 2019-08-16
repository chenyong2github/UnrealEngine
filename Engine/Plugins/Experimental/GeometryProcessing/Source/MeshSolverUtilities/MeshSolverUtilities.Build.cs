// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshSolverUtilities : ModuleRules
{
    public MeshSolverUtilities(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        //PCHUsage = ModuleRules.PCHUsageMode.NoSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "Eigen"
            }
		);

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
				"GeometricObjects",
				"DynamicMesh"
                //"GeometricObjects",
			    //"DynamicMesh"
				//"Slate",
				//"SlateCore",
				// ... add private dependencies that you statically link with here ...	
			}
		);
    }
}