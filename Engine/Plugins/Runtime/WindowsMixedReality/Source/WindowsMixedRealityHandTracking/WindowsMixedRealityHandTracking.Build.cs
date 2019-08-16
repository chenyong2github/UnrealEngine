// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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

            AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");
        }
    }
}
