// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class ControlRigEditor : ModuleRules
    {
        public ControlRigEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
                    "../../../../Source/Editor/UnrealEd/Private", 
					"../../../../Source/Developer/MessageLog/Private", //compatibility for FBX importer
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
