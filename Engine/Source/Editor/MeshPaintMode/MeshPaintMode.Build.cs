// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MeshPaintMode : ModuleRules
{
    public MeshPaintMode(ReadOnlyTargetRules Target) : base(Target)
    {
		PrivateIncludePathModuleNames.AddRange(
            new string[] {
                "AssetRegistry",
                "AssetTools"
            }
        );

        PublicDependencyModuleNames.AddRange(
            new string[] {
                "MeshPaint",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "AppFramework",
                "Core", 
                "CoreUObject",
                "DesktopPlatform",
                "Engine", 
                "InputCore",
                "RenderCore",
                "RHI",
                "Slate",
				"SlateCore",
                "EditorStyle",
				"EditorFramework",
				"UnrealEd",
                "RawMesh",
                "SourceControl",
                "PropertyEditor",
                "MainFrame",
				"MeshPaint",
            }
        );

        PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"AssetTools",
				"LevelEditor"
            });

		DynamicallyLoadedModuleNames.AddRange(
            new string[] {
                "AssetRegistry",
                "AssetTools"
            }
        );
    }
}
