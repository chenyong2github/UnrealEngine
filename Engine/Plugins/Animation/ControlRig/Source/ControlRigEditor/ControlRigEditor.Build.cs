// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class ControlRigEditor : ModuleRules
    {
        public ControlRigEditor(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateIncludePaths.AddRange(
                new string[] {
					System.IO.Path.Combine(EngineDirectory,"Plugins/Animation/ControlRig/Source/ControlRig/Private"),
					System.IO.Path.Combine(EngineDirectory,"Source/Developer/AssetTools/Private"),
					System.IO.Path.Combine(EngineDirectory,"Source/Developer/MessageLog/Private"), //compatibility for FBX importer
					System.IO.Path.Combine(EngineDirectory,"Source/Editor/Kismet/Private"),
					System.IO.Path.Combine(EngineDirectory,"Source/Editor/Persona/Private"),
					System.IO.Path.Combine(EngineDirectory,"Source/Editor/PropertyEditor/Private"),
					System.IO.Path.Combine(EngineDirectory,"Source/Editor/SceneOutliner/Private"),
					System.IO.Path.Combine(EngineDirectory,"Source/Editor/UnrealEd/Private"),
					System.IO.Path.Combine(EngineDirectory,"Source/Runtime/Slate/Private"),
				}
			);

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"MainFrame",
					"AppFramework",
				}
			);

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
					"CurveEditor",
					"Slate",
                    "SlateCore",
                    "InputCore",
                    "Engine",
					"EditorFramework",
					"UnrealEd",
                    "KismetCompiler",
                    "BlueprintGraph",
                    "ControlRig",
                    "ControlRigDeveloper",
                    "Kismet",
					"KismetCompiler",
                    "EditorStyle",
					"EditorWidgets",
                    "ApplicationCore",
                    "AnimationCore",
                    "PropertyEditor",
                    "AnimGraph",
                    "AnimGraphRuntime",
                    "MovieScene",
                    "MovieSceneTracks",
                    "MovieSceneTools",
                    "SequencerCore",
                    "Sequencer",
					"LevelSequenceEditor",
                    "ClassViewer",
                    "AssetTools",
                    "ContentBrowser",
					"ContentBrowserData",
                    "LevelEditor",
                    "SceneOutliner",
                    "EditorInteractiveToolsFramework",
                    "LevelSequence",
                    "GraphEditor",
                    "PropertyPath",
                    "Persona",
                    "UMG",
					"TimeManagement",
                    "PropertyPath",
					"WorkspaceMenuStructure",
					"Json",
					"DesktopPlatform",
					"ToolMenus",
                    "RigVM",
                    "RigVMDeveloper",
					"AnimationEditor",
					"MessageLog",
                    "SequencerScripting",
					"PropertyAccessEditor",
					"KismetWidgets",
					"PythonScriptPlugin",
					"AdvancedPreviewScene",
					"ToolWidgets",
                    "AnimationWidgets",
                    "ActorPickerMode",
                    "Constraints",
                    "AnimationEditMode"
				}
            );

            AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
        }
    }
}
