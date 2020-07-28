// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundStandardNodes : ModuleRules
	{
		public MetasoundStandardNodes(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange
			(
				new string[]
				{
					"Core",
					"Serialization",
					"SignalProcessing",
					"AudioExtensions",
					"MetasoundFrontend"
				}
			);

			PrivateDependencyModuleNames.AddRange
			(
				new string[]
				{
					"MetasoundGraphCore",
					"CoreUObject"
				}
			);
		}
	}
}
