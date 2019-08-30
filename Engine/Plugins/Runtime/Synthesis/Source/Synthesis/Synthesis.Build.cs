// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Synthesis : ModuleRules
	{
        public Synthesis(ReadOnlyTargetRules Target) : base(Target)
		{
            PrivateDependencyModuleNames.AddRange(
				new string[] {
                    "Core",
					"CoreUObject",
					"Engine",
					"AudioMixer",
					"SignalProcessing",
                    "UMG",
                    "Slate",
                    "SlateCore",
                    "InputCore",
                    "Projects"
                }
            );
		}
	}
}