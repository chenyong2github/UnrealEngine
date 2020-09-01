// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MainFrame : ModuleRules
{
	public MainFrame(ReadOnlyTargetRules Target) : base(Target)
	{

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Documentation",
			}
		);

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
				"DerivedDataCache",
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
				"Editor/MainFrame/Private",
				"Editor/MainFrame/Private/Frame",
				"Editor/MainFrame/Private/Menus",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"GameProjectGeneration",
				"ProjectTargetPlatformEditor",
				"LevelEditor",
				"SourceCodeAccess",
				"HotReload",
				"LocalizationDashboard",
			}
		);
	}
}
