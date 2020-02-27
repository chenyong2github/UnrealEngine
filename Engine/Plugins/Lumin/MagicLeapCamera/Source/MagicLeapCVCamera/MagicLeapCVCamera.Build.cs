// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapCVCamera : ModuleRules
	{
		public MagicLeapCVCamera( ReadOnlyTargetRules Target ) : base(Target)
		{
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine"
                }
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Engine",
					"MLSDK",
					"MagicLeap",
					"MagicLeapCamera",
					"HeadMountedDisplay",
				}
			);
		}
	}
}
