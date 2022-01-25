// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class ConsoleVariablesEditor : ModuleRules
{
	public ConsoleVariablesEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"OutputLog"
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Concert",
				"ConcertSyncClient",
				"ConcertSyncCore",
				"ConcertTransport",
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
				"PropertyEditor",
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
