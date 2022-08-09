// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MediaSourceManagerEditor : ModuleRules
{
	public MediaSourceManagerEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"InputCore",
				"MediaAssets",
				"MediaIOCore",
				"MediaPlayerEditor",
				"MediaSourceManager",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
			);
	}
}
