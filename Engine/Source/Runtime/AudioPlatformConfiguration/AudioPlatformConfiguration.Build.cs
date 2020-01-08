// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioPlatformConfiguration : ModuleRules
	{
		public AudioPlatformConfiguration(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
                    "CoreUObject"
                }
			);

            PrivateIncludePathModuleNames.Add("Engine");

            AddEngineThirdPartyPrivateStaticDependencies(Target, "UELibSampleRate");
        }
	}
}
