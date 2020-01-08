// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapHandTracking : ModuleRules
	{
		public MagicLeapHandTracking(ReadOnlyTargetRules Target)
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
					"LuminRuntimeSettings",
					"MagicLeap",
					"MLSDK",
					"SlateCore",
					"LiveLinkInterface",
				}
			);
		}
	}
}
