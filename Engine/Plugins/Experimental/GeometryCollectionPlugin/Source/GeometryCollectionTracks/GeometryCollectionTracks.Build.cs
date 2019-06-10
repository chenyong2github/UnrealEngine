// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class GeometryCollectionTracks : ModuleRules
    {
        public GeometryCollectionTracks(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "MovieScene",
                    "MovieSceneTracks",
                    "GeometryCollectionCore",
                    "GeometryCollectionEngine",
                    "GeometryCollectionSimulationCore",
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
                }
            );


            PublicIncludePathModuleNames.Add("TargetPlatform");

            if (Target.bBuildEditor)
            {
                PublicIncludePathModuleNames.Add("GeometryCollectionSequencer");
                DynamicallyLoadedModuleNames.Add("GeometryCollectionSequencer");
            }
        }
    }
}
