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
			}
		);
		
		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"VirtualTexturingEditor",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"VirtualTexturingEditor",
			}
		);
	}
}
