// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;

public class WorldPartitionHLODUtilities : ModuleRules
{
    public WorldPartitionHLODUtilities(ReadOnlyTargetRules Target) : base(Target)
	{
        PublicIncludePaths.Add("Developer/WorldPartitionHLODUtilities/Public");

        PublicDependencyModuleNames.AddRange(
            new string[]
			{
				"Core",
				"CoreUObject"
			}
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
			{
				"EditorFramework",
				"Engine",
				"MeshDescription",
				"StaticMeshDescription",
				"UnrealEd",
                "Projects",
			}
        );

        PrivateIncludePathModuleNames.AddRange(
            new string[]
            {
				"GeometryProcessingInterfaces",
			}
        );

        DynamicallyLoadedModuleNames.AddRange(
            new string[]
            {
                "MeshUtilities",
                "MeshMergeUtilities",
                "MeshReductionInterface",
				"GeometryProcessingInterfaces",
			}
        );
	}
}
