// Copyright Epic Games, Inc. All Rights Reserved.

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
				"AssetRegistry",
				"ApplicationCore",
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

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AnimationBlueprintEditor",
					"Persona",
					"UnrealEd",
				});
			}
		}
	}
}

