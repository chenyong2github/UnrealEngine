// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class CoreTechSurface : ModuleRules
	{
		public CoreTechSurface(ReadOnlyTargetRules Target) : base(Target)
		{
			// option de compil pour dev
			OptimizeCode = CodeOptimization.Never;
			PCHUsage = PCHUsageMode.NoPCHs;
			// fin option de compil pour dev

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CADInterfaces",
					"CADLibrary",
					"CADTools",
					"Core",
					"CoreUObject",
					"DatasmithContent",
					"DatasmithTranslator",
					"Engine",
					"MeshDescription",
					"ParametricSurface",
					"StaticMeshDescription",
				}
			);
		}
	}
}
