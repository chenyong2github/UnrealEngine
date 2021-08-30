// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class LevelSnapshotsEditor : ModuleRules
{
	public LevelSnapshotsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateIncludePaths.AddRange(
			new string[] 
			{
				Path.Combine(ModuleDirectory, "Private"),
				Path.Combine(ModuleDirectory, "Private", "AssetTypeActions"),
				Path.Combine(ModuleDirectory, "Private", "Customizations"),
				Path.Combine(ModuleDirectory, "Private", "Data"),
				Path.Combine(ModuleDirectory, "Private", "Data", "Filters"),
				Path.Combine(ModuleDirectory, "Private", "Data", "DragDrop"),
				Path.Combine(ModuleDirectory, "Private", "Factories"),
				Path.Combine(ModuleDirectory, "Private", "Misc"),
				Path.Combine(ModuleDirectory, "Private", "TempInterfaces"),
				Path.Combine(ModuleDirectory, "Private", "Toolkits"),
				Path.Combine(ModuleDirectory, "Private", "Views"),
				Path.Combine(ModuleDirectory, "Private", "Views", "Filter"),
				Path.Combine(ModuleDirectory, "Private", "Views", "Input"),
				Path.Combine(ModuleDirectory, "Private", "Views", "Results"),
				Path.Combine(ModuleDirectory, "Private", "Widgets"),
				Path.Combine(ModuleDirectory, "Private", "Widgets", "Filter")
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
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
				"GameProjectGeneration",
				"InputCore",
				"Kismet",
				"LevelSnapshots",
				"LevelSnapshotFilters",
				"Projects",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
			}
			);
	}
}
