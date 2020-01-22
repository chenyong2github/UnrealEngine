// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
    public class ControlRigEditor : ModuleRules
    {
        public ControlRigEditor(ReadOnlyTargetRules Target) : base(Target)
        {
            PrivateIncludePaths.AddRange(
                new string[] {
                    "ControlRigEditor/Private",
                    "ControlRigEditor/Private/Sequencer",
                    "ControlRigEditor/Private/EditMode",
                    "ControlRigEditor/Private/Graph",
                    "ControlRigEditor/Private/Editor",
                    "../../../../Source/Editor/UnrealEd/Private" //compatibility for FBX importer
				}
            );

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"CurveEditor",
				}
	        );

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
                    "Core",
                    "CoreUObject",
                    "Slate",
                    "SlateCore",
                    "InputCore",
                    "Engine",
                    "UnrealEd",
                    "KismetCompiler",
                    "BlueprintGraph",
                    "ControlRig",
                    "ControlRigDeveloper",
                    "Kismet",
                    "EditorStyle",
                    "ApplicationCore",
                    "AnimationCore",
                    "PropertyEditor",
                    "AnimGraph",
                    "AnimGraphRuntime",
                    "MovieScene",
                    "MovieSceneTracks",
                    "MovieSceneTools",
                    "Sequencer",
					"LevelSequenceEditor",
                    "ClassViewer",
                    "AssetTools",
                    "ContentBrowser",
                    "LevelEditor",
                    "SceneOutliner",
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
					"ControlRigManipulation",
                    "RigVM",
                    "RigVMDeveloper"
                }
            );

            AddEngineThirdPartyPrivateStaticDependencies(Target, "FBX");
        }
    }
}
