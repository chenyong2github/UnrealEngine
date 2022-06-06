// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RivermaxMediaEditor : ModuleRules
{
	public RivermaxMediaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[] {
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"AssetTools",
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"MediaAssets",
				"MediaIOCore",
				"MediaIOEditor",
				"RivermaxMedia",
				"Slate",
				"SlateCore",
				"UnrealEd"
			}
		);
	}
}
