// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderResourceViewer : ModuleRules
{
	public RenderResourceViewer(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"WorkspaceMenuStructure",
				"ContentBrowser",
				"UnrealEd",
				"AssetDefinition"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"InputCore",
				"SlateCore",
				"Slate",
				"RHI",
			}
		);
	}
}
