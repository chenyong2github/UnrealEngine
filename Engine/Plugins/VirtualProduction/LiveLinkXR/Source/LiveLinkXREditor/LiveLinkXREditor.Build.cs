// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LiveLinkXREditor : ModuleRules
{
	public LiveLinkXREditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateIncludePaths.AddRange(
			new string[] {
				"LiveLinkXREditor",
			}
			);


		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"UnrealEd",
				"Engine",
				"Projects",
				"DetailCustomizations",
				"LiveLinkInterface",
				"BlueprintGraph",
				"LiveLinkXR",
			}
			);


		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"WorkspaceMenuStructure",
				"EditorStyle",
				"SlateCore",
				"Slate",
				"InputCore"
			}
			);
	}
}
