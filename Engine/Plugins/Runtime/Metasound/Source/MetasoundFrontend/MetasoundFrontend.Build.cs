// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundFrontend : ModuleRules
	{
		public MetasoundFrontend(ReadOnlyTargetRules Target) : base(Target)
		{
			OptimizeCode = CodeOptimization.Never;

			PublicDependencyModuleNames.AddRange
			(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Serialization",
					"SignalProcessing"
				}
			);

			PrivateDependencyModuleNames.AddRange
			(
				new string[]
				{
					"MetasoundGraphCore",
				}
			);

			PublicDefinitions.Add("WITH_METASOUND_FRONTEND=1");
		}
	}
}
