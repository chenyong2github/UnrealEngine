// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class InterchangeFbxParser : ModuleRules
	{
		public InterchangeFbxParser(ReadOnlyTargetRules Target) : base(Target)
		{
			bEnableExceptions = true;

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"InterchangeCore",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"InterchangeMessages",
					"InterchangeNodes",
					"MeshDescription",
					"SkeletalMeshDescription",
					"StaticMeshDescription"
				}
			);

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[] {
						"Engine",
					}
				);
			}

			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"FBX"
			);
		}
	}
}
