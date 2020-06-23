// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace UnrealBuildTool.Rules
{
	public class WinDualShock : ModuleRules
	{
		public WinDualShock(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"Engine",
					"Slate",
					"InputDevice"
			});

			// Use reflection to allow type not to exist if console code is not present
			System.Type LibScePadType = System.Type.GetType("LibScePad");
			bool bHasSupport = false;
			if (LibScePadType != null)
			{
				bHasSupport = (bool)LibScePadType.GetMethod("GetPadLibLocation").Invoke(null, new object[] { EngineDirectory, Target.WindowsPlatform.Compiler, null });
				if (bHasSupport)
				{
					AddEngineThirdPartyPrivateStaticDependencies(Target, new string[] { "LibScePad" });
					PrivateIncludePathModuleNames.AddRange(new string[]
					{
						"ApplicationCore_Sony",
						"ApplicationCore_PS4"
					});
				}
			}

			PublicDefinitions.Add("DUALSHOCK4_SUPPORT=" + (bHasSupport ? "1" : "0"));
		}
	}
}