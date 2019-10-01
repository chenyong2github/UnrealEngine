// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapMedia : ModuleRules
	{
		public MagicLeapMedia(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Media"
			});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"Engine",
					"RenderCore",
					"MediaUtils",
					"LuminRuntimeSettings",
					"MLSDK",
                    "MagicLeap",
                    "MagicLeapHelperVulkan",
					"MagicLeapHelperOpenGL"
            });

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Media"
			});

			PrivateIncludePaths.AddRange(
				new string[] {
                    "MagicLeapMedia/Private"
            });
		}
	}
}
