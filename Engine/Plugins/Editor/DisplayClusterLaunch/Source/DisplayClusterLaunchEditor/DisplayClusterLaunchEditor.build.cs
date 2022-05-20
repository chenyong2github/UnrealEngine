// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class DisplayClusterLaunchEditor : ModuleRules
{
	public DisplayClusterLaunchEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"AssetRegistry",
				"AssetTools",
				"Concert",
				"ConcertTransport",
				"CoreUObject",
				"ConcertSyncClient",
				"ConsoleVariablesEditor",
				"ContentBrowser",
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"Engine",
				"EditorStyle",
				"EditorWidgets",
				"InputCore",
				"Kismet",
				"Messaging",
				"MultiUserClient",
				"Networking",
				"Projects",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"ToolMenus", 
				"ToolWidgets",
				"UdpMessaging",
				"UnrealEd",
				"WorkspaceMenuStructure"
			}
		);
	}
}
