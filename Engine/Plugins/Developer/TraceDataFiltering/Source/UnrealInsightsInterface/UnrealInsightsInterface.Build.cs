// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class UnrealInsightsInterface : ModuleRules
	{
		public UnrealInsightsInterface(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"EditorStyle",
					"Slate",
					"SlateCore",
					"EditorFramework",
					"ApplicationCore",
					"ToolMenus",
					"UATHelper",
				}
			);
		}
	}
}
