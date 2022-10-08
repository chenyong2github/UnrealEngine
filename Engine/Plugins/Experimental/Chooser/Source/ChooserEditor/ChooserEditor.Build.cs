// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ChooserEditor : ModuleRules
	{
		public ChooserEditor(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AssetTools",
					"Chooser",
					"UnrealEd",
					"EditorWidgets",
					"SlateCore",
					"Slate",
					"PropertyEditor",
					"InputCore",
					"EditorStyle",
					"PropertyAccessEditor",
					"PropertyEditor",
					"BlueprintGraph"
					// ... add private dependencies that you statically link with here ...
				}
			);
		}
	}
}