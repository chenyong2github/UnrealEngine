// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundFrontend : ModuleRules
	{
        public MetasoundFrontend(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"SignalProcessing"
                }
            );

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"MetasoundGraphCore",
					"CoreUObject",
					"Serialization"
				}
			);
		}
	}
}
