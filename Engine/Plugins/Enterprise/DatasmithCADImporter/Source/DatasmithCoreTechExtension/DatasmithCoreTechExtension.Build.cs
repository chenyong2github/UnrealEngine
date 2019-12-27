// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DatasmithCoreTechExtension : ModuleRules
	{
		public DatasmithCoreTechExtension(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"MeshDescription",
					"DataprepCore",
					"DatasmithContent",
					"DatasmithImporter",
					"Engine",
					"StaticMeshEditor",
					"Slate",
					"SlateCore",
					"StaticMeshDescription",
					"UnrealEd",
					"CADLibrary",
				}
			);
		}
	}
}
