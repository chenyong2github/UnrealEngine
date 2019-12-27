// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapEyeTracker : ModuleRules
	{
		public MagicLeapEyeTracker(ReadOnlyTargetRules Target)
				: base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"EyeTracker"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"HeadMountedDisplay",
					"Core",
					"CoreUObject",
					"ApplicationCore",
					"Engine",
					"InputCore",
					"Slate",
					"SlateCore",
					"MagicLeap",
					"MLSDK"
				}
			);
		}
	}
}
