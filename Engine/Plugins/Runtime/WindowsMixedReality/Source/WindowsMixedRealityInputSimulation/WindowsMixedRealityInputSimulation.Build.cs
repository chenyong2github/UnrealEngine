// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class WindowsMixedRealityInputSimulation : ModuleRules
	{
		public WindowsMixedRealityInputSimulation(ReadOnlyTargetRules Target)
				: base(Target)
		{
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"HeadMountedDisplay",
					"WindowsMixedRealityHandTracking",
					"WindowsMixedRealityRuntimeSettings",
				}
			);
		}
	}
}
