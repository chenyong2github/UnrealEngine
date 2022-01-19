// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class NaniteTools : ModuleRules
{
	public NaniteTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"EditorConfig",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
                "AssetTools",
				"EditorWidgets",
			}
		);

		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

		PrivateIncludePaths.AddRange(
			new string[] {
				Path.Combine(EngineDir, "Source/Runtime/Renderer/Private")
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
                "RHI",
                "Renderer",
                "InputCore",
				"Slate",
				"SlateCore",
                "EditorStyle",
				"EditorFramework",
				"ToolWidgets",
				"UnrealEd",
				"MeshDescription",
				"StaticMeshDescription",
				"ContentBrowserData",
				"Settings",
            }
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
                "AssetTools",
				"EditorWidgets",
			}
		);
	}
}
