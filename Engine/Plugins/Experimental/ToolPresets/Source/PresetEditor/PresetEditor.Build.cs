// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PresetEditor : ModuleRules
	{
		public PresetEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",					
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"ApplicationCore",
					"CoreUObject",
					"EditorFramework",
					"EditorStyle",
					"Engine",
					"InputCore",
					"Slate",
					"SlateCore",
					"ToolWidgets",
					"PresetAsset",
					"UnrealEd",
					"Projects",
					"DeveloperSettings",
					"ContentBrowser",
					"ContentBrowserData",
					"EditorConfig",
					"AssetTools",
				}
			);
		}
	}
}
