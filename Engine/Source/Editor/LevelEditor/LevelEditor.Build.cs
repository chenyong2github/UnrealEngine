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
				"ApplicationCore",
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
				"EditorFramework",
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
				"TypedElementFramework",
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
				"StatusBar",
				"AppFramework",
				"EditorSubsystem",
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
				"WorldPartitionEditor",
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
				"LevelAssetEditor",
            }
		);

		if(Target.bWithLiveCoding)
		{
			PrivateIncludePathModuleNames.Add("LiveCoding");
		}
	}
}
