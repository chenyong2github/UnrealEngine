// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapBLE : ModuleRules
	{
		public MagicLeapBLE(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"MagicLeap",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"LuminRuntimeSettings",
					"MLSDK",
					"MagicLeap",
					"HeadMountedDisplay",
				}
			);
		}
	}
}
