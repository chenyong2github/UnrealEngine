// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VPMaterialsEditor : ModuleRules
{
	public VPMaterialsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"EditorStyle",
				"Engine",
				"InputCore",
				"Landscape",
				"PropertyEditor",
				"Slate",
				"SlateCore",
				"UnrealEd",
            }
		);
	}
}
