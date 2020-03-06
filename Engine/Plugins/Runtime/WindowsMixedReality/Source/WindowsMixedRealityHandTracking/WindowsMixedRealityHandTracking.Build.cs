// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityHandTracking : ModuleRules
	{
		public WindowsMixedRealityHandTracking(ReadOnlyTargetRules Target)
				: base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"InputDevice"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"InputCore",
					"Slate",
					"HeadMountedDisplay",
					"WindowsMixedRealityHMD",
					"SlateCore",
	                "LiveLink",
                    "LiveLinkInterface"
                }
			);
			
			if (Target.Platform == UnrealTargetPlatform.Win64 && Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("WindowsMixedRealityInputSimulation");
				PrivateDefinitions.Add("WITH_INPUT_SIMULATION=1");
			}
			else
			{
				PrivateDefinitions.Add("WITH_INPUT_SIMULATION=0");
			}

            AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");
        }
    }
}
