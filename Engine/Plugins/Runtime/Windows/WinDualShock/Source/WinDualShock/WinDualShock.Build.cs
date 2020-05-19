// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace UnrealBuildTool.Rules
{
	public class WinDualShock : ModuleRules
	{
		public WinDualShock(ReadOnlyTargetRules Target) : base(Target)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, new string[] { "LibScePad" });

			PrivateIncludePathModuleNames.AddRange(new string[]
			{
				"ApplicationCore_Sony",
				"ApplicationCore_PS4"
			});

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"Engine",
					"Slate",
					"InputDevice"
				}
				);

			if (Target.Platform != UnrealTargetPlatform.PS4)
			{
				String PadLibLocation;
				bool bFoundPadLib = LibScePad.GetPadLibLocation(EngineDirectory, Target.WindowsPlatform.Compiler, out PadLibLocation);
				PublicDefinitions.Add("DUALSHOCK4_SUPPORT=" + (bFoundPadLib ? "1" : "0"));
			}
		}
	}
}
