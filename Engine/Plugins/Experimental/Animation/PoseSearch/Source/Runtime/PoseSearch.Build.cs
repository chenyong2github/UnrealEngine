// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PoseSearch : ModuleRules
{
	public PoseSearch(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		AddEngineThirdPartyPrivateStaticDependencies(Target,
			"Eigen",
			"nanoflann"
		);

		PublicIncludePaths.AddRange(
			new string[]
			{
				System.IO.Path.Combine(ModuleDirectory, "Public"),
			}
		);

		PrivateIncludePaths.AddRange(
			new string[]
			{
				System.IO.Path.Combine(ModuleDirectory, "Private"),
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"AnimationCore",
				"TraceLog",
				"AnimGraphRuntime",
				"DynamicPlayRate",
				"GameplayTags",
				"MotionTrajectory"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
            {
				"Core",
				"CoreUObject",
				"Engine"
			}
		);

		if (Target.bCompileAgainstEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"DerivedDataCache"
				}
			);
		}
	}
}
