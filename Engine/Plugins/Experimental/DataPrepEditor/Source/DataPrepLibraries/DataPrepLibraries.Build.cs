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
					"DatasmithContent",
					"EditorScriptingUtilities",
					"EditorStyle",
					"Engine",
					"GraphEditor",
					"InputCore",
					"KismetCompiler",
					"MeshDescription",
					"MeshDescriptionOperations",
					"Slate",
					"SlateCore",
					"UnrealEd",
				}
			);
		}
	}
}
