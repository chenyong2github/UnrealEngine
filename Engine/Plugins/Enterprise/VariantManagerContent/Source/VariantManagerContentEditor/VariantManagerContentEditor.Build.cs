// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class VariantManagerContentEditor : ModuleRules
	{
		public VariantManagerContentEditor(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"EditorStyle",
					"InputCore", // For ListView keyboard control
					"Slate",
					"SlateCore",
					"ToolMenus",
					"UnrealEd",
					"VariantManagerContent",
					"WorkspaceMenuStructure",
				}
			);
		}
	}
}