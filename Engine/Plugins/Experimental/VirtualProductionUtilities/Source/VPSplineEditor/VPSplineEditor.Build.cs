// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VPSplineEditor : ModuleRules
{
	public VPSplineEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
        PrivateIncludePaths.Add("VPSpline/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
                "Core",
				"CoreUObject",
                "ComponentVisualizers",
				"DetailCustomizations",
				"Engine",
				"LevelSequence",
				"LevelSequenceEditor",
				"Sequencer",
				"SlateCore",
				"Slate",
				"UnrealEd",
				"VPSpline",
				"PropertyEditor",
				"InputCore",
			});

		PublicDependencyModuleNames.AddRange(
			new string[] {
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
			});
	}
}
