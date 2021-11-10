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
				"DeveloperToolSettings",
				"EngineSettings",
				"InputCore",
				"RHI",
				"RenderCore",
				"Slate",
				"SlateCore",
				"TargetPlatform",
				"DesktopPlatform",
				"WorkspaceMenuStructure",
				"MessageLog",
 				"Projects",
 				"ToolMenus",
 				"LauncherServices",
				"SourceControl",
				"EditorStyle",
				"TurnkeyIO",
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
				"UnrealEd",
				"UATHelper",
 				"SettingsEditor",
				"Zen",
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
					"LocalizationDashboard",
	 				"MainFrame",
				}
			);
		}


		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/TurnkeySupport/Private",
			}
		);
	}
}
