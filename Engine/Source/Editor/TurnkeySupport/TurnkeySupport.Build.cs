// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TurnkeySupport : ModuleRules
{
	public TurnkeySupport(ReadOnlyTargetRules Target) : base(Target)
	{

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"Engine",
				"EngineSettings",
				"InputCore",
				"RHI",
				"RenderCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"SourceControl",
				"SourceControlWindows",
				"TargetPlatform",
				"DesktopPlatform",
				"EditorFramework",
				"UnrealEd",
				"WorkspaceMenuStructure",
				"MessageLog",
				"UATHelper",
				"TranslationEditor",
				"Projects",
				"DeviceProfileEditor",
				"UndoHistory",
				"Analytics",
				"ToolMenus",
				"LauncherServices",
				
				"SettingsEditor"
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"DesktopPlatform",
				"GameProjectGeneration",
				"ProjectTargetPlatformEditor",
				"LevelEditor",
				"Settings",
				"SourceCodeAccess",
				"Toolbox",
				"LocalizationDashboard",
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/TurnkeySupport/Private",
			}
		);
	}
}
