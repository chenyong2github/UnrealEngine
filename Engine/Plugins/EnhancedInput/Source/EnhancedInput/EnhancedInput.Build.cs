// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class EnhancedInput : ModuleRules
	{
        public EnhancedInput(ReadOnlyTargetRules Target) : base(Target)
        {
			PrivateIncludePaths.AddRange(
				new string[] {
					"EnhancedInput/Private",
				}
			);

            PrivateDependencyModuleNames.AddRange(
				new string[] {
                    "ApplicationCore",
					"Core",
					"CoreUObject",
                    "Engine",
					"InputCore",
					"Slate",
                    "SlateCore",
                    "DeveloperSettings",
                }
            );
        }
    }
}