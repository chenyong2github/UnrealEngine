// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class DatasmithExporter : ModuleRules
	{
		public DatasmithExporter(ReadOnlyTargetRules Target)
            : base(Target)
		{
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"DatasmithCore",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"CoreUObject",
					"FreeImage",
					"Projects",
                    "RawMesh",
                    "MeshDescription",
					"MeshDescriptionOperations",
					"StaticMeshDescription"
                }
            );

			PrivateIncludePaths.AddRange(
				new string[]
				{
					"Runtime/Launch/Public"
				}
			);

// 			PrecompileForTargets = PrecompileTargetsType.Any;
        }
	}
}