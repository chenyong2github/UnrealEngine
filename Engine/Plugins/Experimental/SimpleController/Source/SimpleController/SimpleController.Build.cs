// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class SimpleController : ModuleRules
	{
        public SimpleController(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateIncludePaths.AddRange(
				new string[] {
					"SimpleController/Private",
					// ... add other private include paths required here ...
				}
				);

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					"Core",
                    "CoreUObject",
                    "Engine",
                    "Slate",
                    "SlateCore",
                    "InputCore"
                }
                );

			PublicDependencyModuleNames.Add("OpenXRHMD");

			// Required to (delayload) link with OpenXR loader.
			AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenXR");
        }
	}
}
