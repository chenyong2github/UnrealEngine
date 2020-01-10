// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ToolMenusEditor : ModuleRules
	{
		public ToolMenusEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"EditorStyle",
					"UnrealEd",
					"InputCore",
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"AssetRegistry"
			});

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"AssetRegistry"
				});
		}
	}
}
