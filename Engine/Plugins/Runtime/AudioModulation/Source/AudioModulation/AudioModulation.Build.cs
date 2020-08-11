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
					"Core",
					"AudioExtensions"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"CoreUObject",
					"Engine",
					"AudioMixer",
					"SignalProcessing",
					"DeveloperSettings"
				}
			);

			if (Target.Type == TargetType.Editor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"Slate",
						"SlateCore",
					}
				);
			}

			PublicDefinitions.Add("WITH_AUDIOMODULATION=1");
		}
	}
}