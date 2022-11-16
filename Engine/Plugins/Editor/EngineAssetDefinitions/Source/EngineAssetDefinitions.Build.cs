// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class EngineAssetDefinitions : ModuleRules
{
	public EngineAssetDefinitions(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ContentBrowser",
				"AssetDefinition",
				"UnrealEd",
				"ToolMenus",
				"CoreUObject",
				"RHI",
				"Engine",
				"AssetRegistry",
				"AssetTools",
				"SlateCore",
				"InputCore",
				"Slate",
				"Kismet",
				"TextureEditor",
				"MaterialEditor",
				"DesktopPlatform",
			}
		);
		
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"VirtualTexturingEditor",
				"CurveAssetEditor",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"VirtualTexturingEditor",
				"CurveAssetEditor",
			}
		);
	}
}
