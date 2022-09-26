// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class CustomizableObjectPopulationEditor : ModuleRules
{
	private int ReadEngineMajorVersion()
	{
		string line;

		System.IO.StreamReader file =
		   new System.IO.StreamReader("Runtime/Launch/Resources/Version.h");

		while ((line = file.ReadLine()) != null)
		{
			if (line.StartsWith("#define ENGINE_MAJOR_VERSION"))
			{
				file.Close();
				return int.Parse(line.Split()[2]);
			}
		}

		file.Close();
		return 0;
	}

	private int ReadEngineMinorVersion()
	{
		string line;

		System.IO.StreamReader file =
		   new System.IO.StreamReader("Runtime/Launch/Resources/Version.h");

		while ((line = file.ReadLine()) != null)
		{
			if (line.StartsWith("#define ENGINE_MINOR_VERSION"))
			{
				file.Close();
				return int.Parse(line.Split()[2]);
			}
		}

		file.Close();
		return 0;
	}

	public CustomizableObjectPopulationEditor(ReadOnlyTargetRules TargetRules) : base(TargetRules)
	{
		ShortName = "MuCOPE";

		DefaultBuildSettings = BuildSettingsVersion.V2;

		PrivateIncludePathModuleNames.AddRange(
			new string[] {
				"AssetRegistry",
				"Kismet",
				"EditorWidgets",
				"MeshUtilities",
				"ContentBrowser",
				"SkeletonEditor",
				"Persona",
				"WorkspaceMenuStructure",
				"AdvancedPreviewScene",

				"MessageLog",
				"KismetWidgets",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				
				"Slate",
				"SlateCore",
				"RenderCore",
				"RHI",
				"UnrealEd",
				"TargetPlatform",
				"RawMesh",
				"PropertyEditor",
				"LevelEditor",
				"AssetTools",
				"GraphEditor",
				"InputCore",
				"Kismet",
				"AdvancedPreviewScene",
				"AppFramework",
				"Projects",
				"ClothingSystemRuntimeCommon",

				"MutableRuntime",
				"MutableTools",
			}
		);

		if (ReadEngineMajorVersion() >= 4 && ReadEngineMinorVersion() >= 18)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"ToolMenus",
			});
		}

		DynamicallyLoadedModuleNames.AddRange(
			new string[] {
				"MainFrame",
				"SceneOutliner",
				"ClassViewer",
				"ContentBrowser",
				"WorkspaceMenuStructure",
			}
		);

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"PropertyEditor",
				"CustomizableObject",
				"CustomizableObjectEditor",
				"CustomizableObjectPopulation",
			}
		);
	}
}
