// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

namespace UnrealBuildTool.Rules
{
	public class AudioSynesthesia : ModuleRules
	{
        public AudioSynesthesia(ReadOnlyTargetRules Target) : base(Target)
		{

            //OptimizeCode = CodeOptimization.Never;
			
            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"CoreUObject",
					"Engine",
					"AudioMixer",
                    "AudioAnalyzer"
                }
            );
		}
	}
}
