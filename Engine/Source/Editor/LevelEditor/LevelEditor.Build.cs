// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LevelEditor : ModuleRules
{
	public LevelEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"ClassViewer",
				"MainFrame",
                "PlacementMode",
				"SlateReflector",
                "IntroTutorials",
                "AppFramework",
                "PortalServices",
                "Persona",
            }
		);

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"Settings",
				"IntroTutorials",
				"HeadMountedDisplay",
				"UnrealEd",
				"VREditor",
				"CommonMenuExtensions"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"LevelSequence",
				"Analytics",
				"Core",
				"CoreUObject",
				"LauncherPlatform",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"Engine",
				"MessageLog",
				"SourceControl",
				"SourceControlWindows",
				"StatsViewer",
				"UnrealEd", 
				"DeveloperSettings",
				"RenderCore",
				"DeviceProfileServices",
				"ContentBrowser",
				"SceneOutliner",
				"ActorPickerMode",
				"RHI",
				"Projects",
				"TargetPlatform",
				"EngineSettings",
				"PropertyEditor",
				"Kismet",
				"KismetWidgets",
				"Sequencer",
				"Foliage",
				"HierarchicalLODOutliner",
				"HierarchicalLODUtilities",
				"MaterialShaderQualitySettings",
				"PixelInspectorModule",
				"CommonMenuExtensions",
				"ToolMenus",
				"EnvironmentLightingViewer",
				"DesktopPlatform",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"MainFrame",
				"ClassViewer",
				"DeviceManager",
				"SettingsEditor",
				"SessionFrontend",
				"SlateReflector",
				"AutomationWindow",
				"Layers",
                "WorldBrowser",
				"EditorWidgets",
				"AssetTools",
				"WorkspaceMenuStructure",
				"NewLevelDialog",
				"DeviceProfileEditor",
                "PlacementMode",
                "IntroTutorials",
				"HeadMountedDisplay",
				"VREditor",
                "Persona",
            }
		);

		if(Target.bWithLiveCoding)
		{
			PrivateIncludePathModuleNames.Add("LiveCoding");
		}
	}
}
