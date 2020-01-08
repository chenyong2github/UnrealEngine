// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealBuildTool.Rules
{
	public class MagicLeapCameraPreview : ModuleRules
	{
		public MagicLeapCameraPreview(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));

			DynamicallyLoadedModuleNames.Add("Media");

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"Engine",
					"RenderCore",
					"MediaUtils",
					"LuminRuntimeSettings",
					"MLSDK",
					"MagicLeap",
					"MagicLeapHelperVulkan",
					"MagicLeapHelperOpenGL"
				}
			);

			PrivateIncludePathModuleNames.AddRange(
				new string[]
				{
					"MagicLeapCamera",
					"Media"
				}
			);

			PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));

			if (Target.Platform == UnrealTargetPlatform.Lumin)
			{
				DynamicallyLoadedModuleNames.Add("MagicLeapCamera");
			}
		}
	}
}
