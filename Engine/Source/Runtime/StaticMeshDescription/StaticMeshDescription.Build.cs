// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class StaticMeshDescription : ModuleRules
	{
		public StaticMeshDescription(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateIncludePaths.Add("Runtime/StaticMeshDescription/Private");
            PublicIncludePaths.Add("Runtime/StaticMeshDescription/Public");

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"MeshDescription"
				}
			);
		}
	}
}
