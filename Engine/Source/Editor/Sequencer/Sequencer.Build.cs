// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Sequencer : ModuleRules
{
	public Sequencer(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(
            new string[] {
                "Editor/Sequencer/Private",
                "Editor/Sequencer/Private/DisplayNodes",
				"Editor/UnrealEd/Private" // TODO: Fix this, for now it's needed for the fbx exporter
	}
        );

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"TimeManagement",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AppFramework", 
				"ApplicationCore",
				"AssetRegistry",
				"CinematicCamera",
				"ContentBrowser",
				"Core", 
				"CoreUObject", 
                "CurveEditor",
                "InputCore",
				"Engine", 
				"Slate", 
				"SlateCore",
                "EditorStyle",
				"UnrealEd", 
				"MovieScene", 
				"MovieSceneTracks", 
				"MovieSceneTools", 
				"MovieSceneCapture", 
                "MovieSceneCaptureDialog", 
				"EditorWidgets", 
				"SequencerWidgets",
				"BlueprintGraph",
				"LevelSequence",
				"GraphEditor",
                "ViewportInteraction",
                "SerializedRecorderInterface",
				"ToolMenus",
			}
		);

		CircularlyReferencedDependentModules.AddRange(
			new string[]
			{
				"ViewportInteraction",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
                "ContentBrowser",
				"PropertyEditor",
				"Kismet",
                "LevelEditor",
				"MainFrame",
				"DesktopPlatform",
                "SerializedRecorderInterface"
            }
        );

		PublicIncludePathModuleNames.AddRange(
			new string[] {
				"PropertyEditor",
                "SceneOutliner",
                "CurveEditor",
                "Analytics",
            }
        );

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"PropertyEditor",
				"LevelEditor",
				"SceneOutliner",
				"WorkspaceMenuStructure",
				"MainFrame",
			}
		);

		CircularlyReferencedDependentModules.Add("MovieSceneTools");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
	}
}
