// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VirtualTexturingEditor : ModuleRules
{
	public VirtualTexturingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.Add("Editor/VirtualTexturingEditor/Private");

        PrivateIncludePathModuleNames.AddRange(
            new string[] {
				"AssetRegistry",
				"AssetTools",
				"ContentBrowser",
				"DesktopPlatform",
				"MainFrame",
				"Renderer",
				"UnrealEd",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"AppFramework",
				"AssetRegistry",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"DesktopPlatform",
				"DesktopWidgets",
				"EditorStyle",
				"Engine",
				"InputCore",
				"MaterialEditor",
				"PlacementMode",
				"PropertyEditor",
				"RenderCore",
				"Renderer",
				"RHI",
				"Slate",
				"SlateCore",
				"UnrealEd",
            }
        );
    }
}