// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DMXPixelMappingEditor : ModuleRules
{
	public DMXPixelMappingEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange( new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"Slate",
			"SlateCore",
			"EditorStyle",
			"InputCore",
			"PropertyEditor",
			"RHI",
			"RenderCore",
			"EditorStyle",
			"Projects",
			"DMXRuntime",
			"DMXEditor",
			"DMXPixelMappingCore",
			"DMXPixelMappingEditorWidgets",
			"DMXPixelMappingRuntime",
			"DMXPixelMappingRenderer",
			"DMXPixelMappingBlueprintGraph",
			"ApplicationCore"
		});
	}
}
