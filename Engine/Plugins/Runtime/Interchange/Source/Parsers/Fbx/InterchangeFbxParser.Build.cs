// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class InterchangeFbxParser : ModuleRules
	{
		public InterchangeFbxParser(ReadOnlyTargetRules Target) : base(Target)
		{
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
					"InterchangeNodePlugin",
				}
			);

			if (Target.bCompileAgainstEngine)
			{
				PrivateDependencyModuleNames.Add("Engine");

			}

			AddEngineThirdPartyPrivateStaticDependencies(Target,
				"FBX"
			);
		}
    }
}
