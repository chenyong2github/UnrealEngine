// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
					"PropertyEditor",  // For functions that create the property widgets
					"SlateCore",
					"Slate",
					"EditorStyle", // For standard styles on most of UI
					"InputCore", // For ListView keyboard control
					"GraphEditor", // For DragDropOp, might be removed later
                    "BlueprintGraph", // For function director
					"WorkspaceMenuStructure",
					"ToolMenus",
					"VariantManagerContentEditor",
				}
			);
        }
	}
}