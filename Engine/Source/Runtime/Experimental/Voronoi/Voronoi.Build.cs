// Copyright Epic Games, Inc. All Rights Reserved.
using System;
namespace UnrealBuildTool.Rules
{
	public class Voronoi : ModuleRules
	{
        public Voronoi(ReadOnlyTargetRules Target) : base(Target)
		{
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
