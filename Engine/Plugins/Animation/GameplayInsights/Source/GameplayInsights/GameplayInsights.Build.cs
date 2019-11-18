// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GameplayInsights : ModuleRules
	{
		public GameplayInsights(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"InputCore",
				"SlateCore",
				"Slate",
				"TraceAnalysis",
				"TraceServices",
				"TraceInsights",
			});

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Engine",
					"CoreUObject",
				});
			}

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
				});
			}
		}
	}
}

