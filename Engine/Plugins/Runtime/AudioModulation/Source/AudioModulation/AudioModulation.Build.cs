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
					"SignalProcessing"
				}
			);

			PublicDefinitions.Add("WITH_AUDIOMODULATION=1");
		}
	}
}