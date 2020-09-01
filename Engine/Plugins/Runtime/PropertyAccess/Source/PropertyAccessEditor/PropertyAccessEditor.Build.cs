// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class PropertyAccessEditor : ModuleRules
{
	public PropertyAccessEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Slate",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"PropertyAccess",
				"GraphEditor",
				"BlueprintGraph",
				"EditorStyle",
				"Engine",
				"UnrealEd",
				"SlateCore",
				"InputCore",
				"KismetWidgets",
				"PropertyPath",
				"AnimGraph",
			}
		);
	}
}
