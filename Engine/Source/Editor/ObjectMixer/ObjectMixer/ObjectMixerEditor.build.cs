// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ObjectMixerEditor : ModuleRules
{
	public ObjectMixerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"OutputLog",
				"PropertyEditor"
			}
		);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"AssetTools",
				"CoreUObject",
				"ContentBrowser",
				"Engine",
				"EditorStyle",
				"EditorWidgets",
				"InputCore",
				"Kismet",
				"Projects",
				"SceneOutliner",
				"Slate",
				"SlateCore",
				"ToolMenus", 
				"ToolWidgets",
				"UnrealEd",
				"WorkspaceMenuStructure"
			}
		);
	}
}
