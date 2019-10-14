// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DatasmithCore : ModuleRules
	{
		public DatasmithCore(ReadOnlyTargetRules Target)
            : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"RawMesh",
					"MeshDescription",
					"MeshDescriptionOperations",
					"StaticMeshDescription"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Json",
					"XmlParser",
				}
			);
        }
	}
}