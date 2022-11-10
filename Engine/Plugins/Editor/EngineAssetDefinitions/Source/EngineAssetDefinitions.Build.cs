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
				"Engine",
				"AssetTools",
				"SlateCore",
				"Slate",
				"Kismet"
			}
		);
	}
}
