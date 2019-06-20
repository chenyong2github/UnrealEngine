// Copyright (c) Microsoft Corporation. All rights reserved.

using System;
using System.IO;
using UnrealBuildTool;
using Microsoft.Win32;

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityEyeTracker :
		ModuleRules
	{
		public WindowsMixedRealityEyeTracker(ReadOnlyTargetRules Target)
			: base(Target)
		{
			bEnableExceptions = true;

            AddEngineThirdPartyPrivateStaticDependencies(Target, "WindowsMixedRealityInterop");

            if (Target.Platform == UnrealTargetPlatform.Win32 ||
				Target.Platform == UnrealTargetPlatform.Win64 ||
				Target.Platform == UnrealTargetPlatform.HoloLens)
			{
				PublicDependencyModuleNames.AddRange(
					new string[]
					{
						"MixedRealityInteropLibrary",
						"InputDevice",
						"EyeTracker",
					}
				);

				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"Core",
						"CoreUObject",
						"Engine",
						"InputCore",
					}
				);

                PublicDefinitions.Add("WITH_WINDOWS_MIXED_REALITY=1");
            }
        }
	}
}