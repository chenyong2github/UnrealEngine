// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class LensDistortionEditor : ModuleRules
	{
		public LensDistortionEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AssetRegistry",
					"AssetTools",
					"Core",
					"CoreUObject",
					"EditorStyle",
					"Engine",
					"LensDistortion",
					"PropertyEditor",
					"Slate",
					"SlateCore",
					"UnrealEd",
					"WorkspaceMenuStructure",
				});
		}
	}
}
