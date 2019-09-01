// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioAnalyzer : ModuleRules
	{
		public AudioAnalyzer(ReadOnlyTargetRules Target) : base(Target)
		{
			bUseUnity = true;

            PrivateDependencyModuleNames.AddRange(
				new string[]
				{
                    "InputCore",
                    "CoreUObject",
                    "Engine"
                }
			);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
                    "Core",
					"SignalProcessing"
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
