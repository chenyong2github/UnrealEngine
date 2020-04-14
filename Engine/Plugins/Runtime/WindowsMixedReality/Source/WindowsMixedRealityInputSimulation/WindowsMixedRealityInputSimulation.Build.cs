// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityInputSimulation : ModuleRules
	{
		public WindowsMixedRealityInputSimulation(ReadOnlyTargetRules Target)
				: base(Target)
		{
			PublicIncludePathModuleNames.AddRange(
				new string[]
				{
					"WindowsMixedRealityHandTracking"
				}
			);

            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"HeadMountedDisplay",
					"WindowsMixedRealityHMD"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"WindowsMixedRealityRuntimeSettings",
				}
			);

            if (Target.bBuildEditor)
            {
                PrivateDependencyModuleNames.Add("UnrealEd");
            }
        }
	}
}
