// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class GeometryCollectionSequencer : ModuleRules
    {
        public GeometryCollectionSequencer(ReadOnlyTargetRules Target) : base(Target)
        {
            PublicIncludePathModuleNames.AddRange(
                new string[] {
                "Sequencer",
                }
            );

            PrivateIncludePaths.Add("GeometryCollectionSequencer/Private");

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "AssetTools",
                    "Core",
                    "CoreUObject",
                    "EditorStyle",
                    "Engine",
                    "MovieScene",
                    "MovieSceneTools",
                    "MovieSceneTracks",
                    "RHI",
                    "Sequencer",
                    "Slate",
                    "SlateCore",
                    "TimeManagement",
                    "UnrealEd",
                    "GeometryCollectionTracks",
                    "GeometryCollectionCore",
                    "GeometryCollectionEngine",
                    "GeometryCollectionSimulationCore",
                }
            );
        }
    }
}
