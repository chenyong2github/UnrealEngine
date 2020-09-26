// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterConfigurator : ModuleRules
{
	public DisplayClusterConfigurator(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AdvancedPreviewScene",
				"AssetTools",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"GraphEditor",
				"EditorStyle",
				"EditorSubsystem",
				"EditorWidgets",
				"Engine",
				"InputCore",
				"MessageLog",
				"PinnedCommandList",
				"Projects",
				"Serialization",
				"Settings",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd"
			});
	}
}
