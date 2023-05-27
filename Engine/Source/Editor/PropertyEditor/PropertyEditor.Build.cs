// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PropertyEditor : ModuleRules
{
	public PropertyEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"EditorConfig",
				"EditorFramework",
				"UnrealEd",
                	"ActorPickerMode",
                	"SceneDepthPickerMode",
			}
		);
		
        PublicIncludePathModuleNames.AddRange(
            new string[] {
				"EditorFramework",
			}
        );

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"MainFrame",
               	"AssetRegistry",
                	"AssetTools",
				"ClassViewer",
				"StructViewer",
				"ContentBrowser",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                	"AppFramework",
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Slate",
				"SlateCore",
				"EditorWidgets",
				"Documentation",
                	"RHI",
				"ConfigEditor",
                	"SceneOutliner",
				"DesktopPlatform",
				"PropertyPath",
				"ToolWidgets",
				"Json"
			}
        );

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
                	"AssetRegistry",
                	"AssetTools",
				"ClassViewer",
				"StructViewer",
				"ContentBrowser",
				"MainFrame",
			}
		);
	}
}
