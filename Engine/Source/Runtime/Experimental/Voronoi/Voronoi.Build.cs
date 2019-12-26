// Copyright Epic Games, Inc. All Rights Reserved.
using System;
namespace UnrealBuildTool.Rules
{
	public class Voronoi : ModuleRules
	{
        public Voronoi(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("Runtime/Experimental/Voronoi/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject"
                }
                );
		}
	}
}
