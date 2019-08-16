// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
namespace UnrealBuildTool.Rules
{
	public class PlanarCut : ModuleRules
	{
        public PlanarCut(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("PlanarCut/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Voronoi",
					"GeometryCollectionCore",
					"GeometricObjects",
					"GeometryAlgorithms"
                }
                );
		}
	}
}
