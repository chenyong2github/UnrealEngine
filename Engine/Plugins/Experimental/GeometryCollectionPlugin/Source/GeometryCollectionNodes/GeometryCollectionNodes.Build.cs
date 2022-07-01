// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionNodes : ModuleRules
	{
        public GeometryCollectionNodes(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("GeometryCollectionNodes/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ChaosCore",
					"Chaos",
					"DataflowCore",
					"DataflowEngine",
					"GeometryCollectionEngine",
					"GeometryCore",
					"Voronoi",
					"PlanarCut",
					"Engine"
				}
			);
		}
	}
}
