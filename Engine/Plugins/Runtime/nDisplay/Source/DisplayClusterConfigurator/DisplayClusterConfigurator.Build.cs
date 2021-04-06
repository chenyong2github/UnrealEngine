// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterConfigurator : ModuleRules
{
	public DisplayClusterConfigurator(ReadOnlyTargetRules ROTargetRules) : base(ROTargetRules)
	{
		DefaultBuildSettings = BuildSettingsVersion.V2;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"DisplayCluster",
				"DisplayClusterConfiguration",
				"DisplayClusterProjection",
				"MPCDI",

				"AdvancedPreviewScene",
				"ApplicationCore",
				"AppFramework",
				"AssetTools",
				"CinematicCamera",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"GraphEditor",
				"EditorStyle",
				"EditorSubsystem",
				"EditorWidgets",
				"Engine",
				"InputCore",
				"MainFrame",
				"MessageLog",
				"PinnedCommandList",
				"Projects",
				"PropertyEditor",
				"Serialization",
				"Settings",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
				"Kismet",
				"KismetCompiler",
				"ImageWrapper"
			});
	}
}
