// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class AddContentDialog : ModuleRules
{
	public AddContentDialog(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"ContentBrowser"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"ApplicationCore",
				"Slate",
				"SlateCore",
				"InputCore",
				"Json",
				"DirectoryWatcher",
				"DesktopPlatform",
				"PakFile",
				"ImageWrapper",
				"EditorFramework",
				"UnrealEd",
				"CoreUObject",				
				"WidgetCarousel",
				"ToolWidgets",
			}
		);
	}
}
