// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MediaPlateEditor : ModuleRules
{
	public MediaPlateEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"MediaAssets",
				"MediaCompositing",
				"MediaCompositingEditor",
				"MediaPlate",
				"MediaPlayerEditor",
				"MovieScene",
				"Sequencer",
				"Slate",
				"SlateCore",
				"UnrealEd",
			}
			);
	}
}
