// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class AnimGraphRuntime : ModuleRules
{
	public AnimGraphRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Runtime/AnimGraphRuntime/Private");

		PublicDependencyModuleNames.AddRange(
			new string[] { 
				"Core", 
				"CoreUObject", 
				"Engine",
                "AnimationCore",
				"GeometryCollectionEngine",
				"GeometryCollectionSimulationCore"
			}
		);

        SetupModulePhysicsSupport(Target);

        if (Target.bCompileChaos || Target.bUseChaos)
        {
            PublicDependencyModuleNames.AddRange(
                new string[] {
					"ChaosSolvers",
                }
            );
        }
    }
}
