// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AudioModulation : ModuleRules
	{
		public AudioModulation(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"AudioExtensions",
					"Core"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"AudioMixer",
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
					"MetasoundFrontend",
					"MetasoundGraphCore",
					"SignalProcessing"
				}
			);

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"Slate",
						"SlateCore"
					}
				);
			}

			PublicDefinitions.Add("WITH_AUDIOMODULATION=1");
		}
	}
}