// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StateTreeModule : ModuleRules
	{
		public StateTreeModule(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.AddRange(
				new string[] {
				}
			);

			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
					"AIModule",
					"GameplayTags",
					"StructUtils",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"RenderCore",
					"PropertyPath",
				}
			);
			
			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd"	// Editor callbacks
					}
				);
			}

			if (Target.Configuration != UnrealTargetConfiguration.Shipping || Target.bBuildEditor)
			{
				PublicDefinitions.Add("WITH_STATETREE_DEBUGGER=1");
				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"TraceLog",
						"TraceServices",
						"TraceAnalysis"
					}
				);
			}
			else
			{
				PublicDefinitions.Add("WITH_STATETREE_DEBUGGER=0");
			}
		}
	}
}
