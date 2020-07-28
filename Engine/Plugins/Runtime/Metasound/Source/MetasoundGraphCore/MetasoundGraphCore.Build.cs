// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MetasoundGraphCore : ModuleRules
	{
        public MetasoundGraphCore(ReadOnlyTargetRules Target) : base(Target)
		{
            //OptimizeCode = CodeOptimization.Never;

            PrivateDependencyModuleNames.AddRange(
				new string[] {
                    "AudioExtensions"
                }
			);

            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"SignalProcessing",
                }
            );
		}
	}
}
