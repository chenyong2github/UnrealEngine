// Copyright Epic Games, Inc. All Rights Reserved.

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
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"TraceLog",
			}
		);

        SetupModulePhysicsSupport(Target);

		// External users of this library do not need to know about Eigen.
        AddEngineThirdPartyPrivateStaticDependencies(Target,
                "Eigen"
                );

        if (Target.bCompileChaos || Target.bUseChaos)
        {
            PublicDependencyModuleNames.AddRange(
                new string[] {
					"GeometryCollectionEngine",
                }
            );
        }
    }
}
