// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DatasmithImporter : ModuleRules
	{
		public DatasmithImporter(ReadOnlyTargetRules Target)
			: base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Analytics",
					"CinematicCamera",
					"ContentBrowser",
					"Core",
					"CoreUObject",
					"DesktopPlatform",
					"EditorStyle",
					"Engine",
					"Foliage",
					"FreeImage",
					"InputCore",
					"Json",
					"Landscape",
					"LandscapeEditor",
					"LandscapeEditorUtilities",
					"LevelSequence",
					"MainFrame",
					"MaterialEditor",
					"MeshDescription",
					"MeshDescriptionOperations",
					"MeshUtilities",
					"MeshUtilitiesCommon",
					"MessageLog",
					"MovieScene",
					"MovieSceneTracks",
					"RHI",
					"RawMesh",
					"Slate",
					"SlateCore",
					"SourceControl",
					"StaticMeshDescription",
					"StaticMeshEditorExtension",
					"ToolMenus",
					"UnrealEd",
					"VariantManager",
					"VariantManagerContent",
                }
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"DataprepCore",
					"DatasmithContent",
                    "DatasmithCore",
                    "DatasmithContentEditor",
				}
			);
        }
    }
}