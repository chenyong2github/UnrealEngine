// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ContentBrowserAssetDataSource : ModuleRules
	{
		public ContentBrowserAssetDataSource(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"ContentBrowserData",
				}
			);

			PublicIncludePathModuleNames.AddRange(
				new string[]
				{
					"AssetRegistry",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"AssetRegistry",
					"AssetTools",
					"CollectionManager",
					"Engine",
					"UnrealEd",
					"EditorStyle",
					"EditorWidgets",
					"Projects",
					"ToolMenus",
					"Slate",
					"SlateCore",
					"InputCore",
					"SourceControl",
					"SourceControlWindows",
				}
			);
		}
	}
}
