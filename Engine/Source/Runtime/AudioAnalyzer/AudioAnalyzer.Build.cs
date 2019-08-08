// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioAnalyzer : ModuleRules
	{
		public AudioAnalyzer(ReadOnlyTargetRules Target) : base(Target)
		{

            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
                    "InputCore",
                    "CoreUObject",
                    "Engine"
                }
			);

            if (Target.Type == TargetType.Editor &&
				(Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
                )
            {
                AddEngineThirdPartyPrivateStaticDependencies(Target, "UELibSampleRate");
            }
        }
	}
}
