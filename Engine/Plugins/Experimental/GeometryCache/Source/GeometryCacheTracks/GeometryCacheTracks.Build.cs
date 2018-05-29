// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCacheTracks : ModuleRules
	{
		public GeometryCacheTracks(ReadOnlyTargetRules Target) : base(Target)
		{
            OptimizeCode = CodeOptimization.Never;

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"MovieScene",
					"MovieSceneTracks",
                    "GeometryCache",

                }
            );

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
                    "AnimGraphRuntime",
                    "TimeManagement",
					"Sequencer"
				}
			);


            PublicIncludePathModuleNames.Add("TargetPlatform");

            if (Target.bBuildEditor)
            {
                PublicIncludePathModuleNames.Add("GeometryCacheSequencer");
                DynamicallyLoadedModuleNames.Add("GeometryCacheSequencer");
            }
        }
	}
}
