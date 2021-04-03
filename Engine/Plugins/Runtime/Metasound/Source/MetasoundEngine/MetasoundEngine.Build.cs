// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundEngine : ModuleRules
	{
		public MetasoundEngine(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange
			(
				new string[]
				{
					"Core",
					"MetasoundGraphCore",
					"MetasoundGenerator",
					"AudioExtensions",
					"AudioCodecEngine",
					"SignalProcessing"
				}
			);

			PublicDependencyModuleNames.AddRange
			(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"MetasoundFrontend",
					"MetasoundStandardNodes",
					"AudioCodecEngine",
					"Serialization",
					"AudioPlatformConfiguration",
				}
			);
		}
	}
}
