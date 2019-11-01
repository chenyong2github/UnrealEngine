// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataprepLibraries : ModuleRules
	{
		public DataprepLibraries(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"BlueprintGraph",
					"Core",
					"CoreUObject",
					"DataprepCore",
					"EditorScriptingUtilities",
					"EditorStyle",
					"Engine",
					"GraphEditor",
					"InputCore",
					"KismetCompiler",
					"LevelSequence",
					"MeshDescription",
					"MeshDescriptionOperations",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UnrealEd",
				}
			);
		}
	}
}
