// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class PixelStreamingPlayerFactory : ModuleRules
	{
		public PixelStreamingPlayerFactory(ReadOnlyTargetRules Target) : base(Target)
		{
			DynamicallyLoadedModuleNames.AddRange(
				new string[] {
					"Media",
				});

			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
					"CoreUObject",
					"MediaAssets",
	                "InputDevice",
                });

			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"Media",
                    "PixelStreaming",
				});

			DynamicallyLoadedModuleNames.Add("PixelStreaming");
		}
	}
}
