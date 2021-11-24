// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Persona : ModuleRules
{
    public Persona(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateIncludePaths.Add("Editor/Persona/Private");  // For PCH includes (because they don't work with relative paths, yet)
        PrivateIncludePaths.Add("Editor/Persona/Private/AnimTimeline");

        PublicIncludePathModuleNames.AddRange(
            new string[] {
                "SkeletonEditor",
                "AnimationEditor",
                "AdvancedPreviewScene",
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "AdvancedPreviewScene",
            }
        );

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "AssetRegistry", 
                "MainFrame",
                "DesktopPlatform",
                "ContentBrowser",
                "AssetTools",
                "MeshReductionInterface",
                "SequenceRecorder",
                "AnimationBlueprintEditor",
			}
		);

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "AppFramework",
                "AnimationModifiers",
                "AnimationBlueprintLibrary",
                "Core", 
                "CoreUObject", 
				"ApplicationCore",
                "Slate", 
                "SlateCore",
				"ContentBrowserData",
                "EditorStyle",
                "Engine",
				"EditorFramework",
				"UnrealEd", 
                "GraphEditor", 
                "InputCore",
                "Kismet", 
                "KismetWidgets",
                "AnimGraph",
                "PropertyEditor",
                "EditorWidgets",
                "BlueprintGraph",
                "RHI",
                "Json",
                "JsonUtilities",
                "ClothingSystemEditorInterface",
                "ClothingSystemRuntimeInterface",
                "ClothingSystemRuntimeCommon",
                "AnimGraphRuntime",
                "CommonMenuExtensions",
                "PinnedCommandList",
                "RenderCore",
				"SkeletalMeshUtilitiesCommon",
				"ToolMenus",
                "CurveEditor",
				"SequencerWidgets",
				"TimeManagement",
                "Sequencer",
				"StatusBar",
				"ToolWidgets",
			}
        );

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "ContentBrowser",
                "Documentation",
                "MainFrame",
                "DesktopPlatform",
                "SkeletonEditor",
                "AssetTools",
                "AnimationEditor",
                "MeshReductionInterface",
                "SequenceRecorder",
			}
        );
    }
}
