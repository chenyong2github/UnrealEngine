// Copyright Epic Games, Inc. All Rights Reserved.

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
					"DataprepEditor",
					"DatasmithContent",
					"EditorScriptingUtilities",
					"EditorStyle",
					"Engine",
					"GraphEditor",
					"InputCore",
					"KismetCompiler",
					"LevelSequence",
					"MeshDescription",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"ToolMenus",
					"UnrealEd",
					"PhysicsCore",
				}
			);

			PrivateIncludePaths.AddRange(
				new string[] 
				{
					"DataprepCore/Private/Shared",
				}
			);
		}
	}
}
