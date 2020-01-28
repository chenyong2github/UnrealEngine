// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class VariantManager : ModuleRules
	{
		public VariantManager(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"UnrealEd",
					"PropertyPath",  // For how we handle captured properties
					"VariantManagerContent"  // Data classes are in here
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "BlueprintGraph", // For function director
					"EditorStyle", // For standard styles on most of UI
					"GraphEditor", // For DragDropOp, might be removed later
					"InputCore", // For ListView keyboard control
					"PropertyEditor",  // For functions that create the property widgets
					"SceneOutliner",
					"Slate",
					"SlateCore",
					"ToolMenus",
					"VariantManagerContentEditor",
					"WorkspaceMenuStructure",
				}
			);
        }
	}
}