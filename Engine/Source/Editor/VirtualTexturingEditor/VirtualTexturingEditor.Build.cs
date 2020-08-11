// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VirtualTexturingEditor : ModuleRules
{
	public VirtualTexturingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.Add("Editor/VirtualTexturingEditor/Private");

        PrivateDependencyModuleNames.AddRange(
            new string[] {
				"AppFramework",
				"AssetRegistry",
				"ContentBrowser",
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Landscape",
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