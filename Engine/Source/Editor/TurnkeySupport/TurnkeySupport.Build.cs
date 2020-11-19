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
				"TargetPlatform",
				"DesktopPlatform",
				"WorkspaceMenuStructure",
				"MessageLog",
				"Projects",
				"ToolMenus",
				"LauncherServices",
				"SettingsEditor"
			}
		);

		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"UnrealEd",
					"UATHelper",
					"EditorStyle",
					"SourceControl",
				}
			);
		}

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
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
				"Editor/TurnkeySupport/Private",
			}
		);
	}
}
