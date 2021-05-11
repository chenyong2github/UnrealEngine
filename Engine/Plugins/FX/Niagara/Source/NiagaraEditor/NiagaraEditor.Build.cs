// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class NiagaraEditor : ModuleRules
{
	public NiagaraEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.AddRange(new string[] {
			"NiagaraEditor/Private",
			"NiagaraEditor/Private/Toolkits",
			"NiagaraEditor/Private/Widgets",
			"NiagaraEditor/Private/Sequencer/NiagaraSequence",
			"NiagaraEditor/Private/ViewModels",
			"NiagaraEditor/Private/TypeEditorUtilities"
		});

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Engine",
                "RHI",
                "Core", 
				"CoreUObject", 
				"CurveEditor",
				"ApplicationCore",
                "InputCore",
				"RenderCore",
				"Slate", 
				"SlateCore",
				"SlateNullRenderer",
				"Kismet",
                "EditorStyle",
				"UnrealEd", 
				"VectorVM",
                "NiagaraCore",
                "Niagara",
                "NiagaraShader",
                "MovieScene",
				"Sequencer",
				"TimeManagement",
				"PropertyEditor",
				"GraphEditor",
                "ShaderFormatVectorVM",
                "TargetPlatform",
                "DesktopPlatform",
                "AppFramework",
				"MovieSceneTools",
                "MovieSceneTracks",
                "AdvancedPreviewScene",
				"Projects",
                "MainFrame",
				"ToolMenus",
				"Renderer",
				"EditorWidgets",
				"DeveloperSettings",
				"SessionServices",
				"SessionFrontend",
				"PythonScriptPlugin"
			}
        );

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"Engine",
				"Messaging",
				"LevelEditor",
				"AssetTools",
				"ContentBrowser",
                "DerivedDataCache",
            }
        );

		PublicDependencyModuleNames.AddRange(
            new string[] {
                "NiagaraCore",
                "NiagaraShader",
                "Engine",
                "NiagaraCore",
                "Niagara",
                "UnrealEd",
            }
        );

        PublicIncludePathModuleNames.AddRange(
            new string[] {
				"Engine",
				"Niagara"
			}
		);

        DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "WorkspaceMenuStructure",
                }
            );
	}
}
