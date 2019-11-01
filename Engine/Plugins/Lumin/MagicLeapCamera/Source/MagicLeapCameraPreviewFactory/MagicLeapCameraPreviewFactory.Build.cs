// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MagicLeapCameraPreviewFactory : ModuleRules
	{
		public MagicLeapCameraPreviewFactory(ReadOnlyTargetRules Target) : base(Target)
		{
			DynamicallyLoadedModuleNames.Add("Media");

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"MediaAssets"
				});

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"MagicLeapCameraPreview",
					"Media"
				});

			PrivateIncludePaths.Add("MagicLeapCameraPreviewFactory/Private");

			if (Target.Platform == UnrealTargetPlatform.Lumin)
			{
				DynamicallyLoadedModuleNames.Add("MagicLeapCameraPreview");
			}
		}
	}
}
