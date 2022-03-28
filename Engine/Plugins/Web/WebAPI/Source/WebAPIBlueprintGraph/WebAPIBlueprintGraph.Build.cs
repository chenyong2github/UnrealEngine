// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class WebAPIBlueprintGraph : ModuleRules
{
	public WebAPIBlueprintGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"AssetRegistry",
				"AssetTools",
				"BlueprintGraph",
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"GraphEditor",
				"InputCore",
				"Json",
				"Kismet",
				"KismetCompiler",
				"KismetWidgets",
				"Projects",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
				"WebAPI",
			}
		);
	}
}
