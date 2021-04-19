// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DetailCustomizations : ModuleRules
{
	public DetailCustomizations(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Editor/DetailCustomizations/Private");	// For PCH includes (because they don't work with relative paths, yet)

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework",
				"Core",
// 				"AudioEditor",
				"CoreUObject",
				"ApplicationCore",
				"DesktopWidgets",
				"Engine",
				"Landscape",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorStyle",
				"UnrealEd",
				"EditorWidgets",
				"Kismet",
				"KismetWidgets",
				"MovieSceneCapture",
				"MovieSceneTools",
				"MovieSceneTracks",
                "Sequencer",
                "MovieScene",
                "TimeManagement",
				"SharedSettingsWidgets",
				"ContentBrowser",
				"BlueprintGraph",
				"GraphEditor",
				"AnimGraph",
				"PropertyEditor",
				"LevelEditor",
				"DesktopPlatform",
				"ClassViewer",
				"TargetPlatform",
				"ExternalImagePicker",
				"MoviePlayer",
				"SourceControl",
				"InternationalizationSettings",
				"SourceCodeAccess",
				"RHI",
				"HardwareTargeting",
				"NavigationSystem",
				"AIModule", 
				"ConfigEditor",
				"CinematicCamera",
				"ComponentVisualizers",
				"SkeletonEditor",
				"LevelSequence",
				"AdvancedPreviewScene",
				"AudioSettingsEditor",
				"HeadMountedDisplay",
                "DataTableEditor",
				"ToolMenus",
				"PhysicsCore",
				"RenderCore"
			}
		);

        PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Engine",
				"Media",
				"Landscape",
				"LandscapeEditor",
				"PropertyEditor",
				"GameProjectGeneration",
				"ComponentVisualizers",
				"GraphEditor",
				"MeshMergeUtilities",
				"MeshReductionInterface",
            }
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"Layers",
				"GameProjectGeneration",
				"MeshMergeUtilities",
				"MeshReductionInterface",
            }
		);
	}
}
