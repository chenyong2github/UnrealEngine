// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DataprepCore : ModuleRules
	{
		public DataprepCore(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"BlueprintGraph",
					"Core",
					"CoreUObject",
					"EditorScriptingUtilities",
					"EditorStyle",
					"EditorWidgets",
					"Engine",
					"GraphEditor",
					"InputCore",
					"LevelSequence",
					"KismetCompiler",
					"MeshDescription",
					"MeshDescriptionOperations",
					"PropertyEditor",
					"Slate",
					"SlateCore",
					"UnrealEd",
				}
			);
		}
	}
}
