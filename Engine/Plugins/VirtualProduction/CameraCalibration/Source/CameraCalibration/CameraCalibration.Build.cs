// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CameraCalibration : ModuleRules
	{
		public CameraCalibration(ReadOnlyTargetRules Target) : base(Target)
		{
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
				OptimizeCode = CodeOptimization.Never;
				bUseUnity = false;
				PCHUsage = PCHUsageMode.NoPCHs;
			}

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"CameraCalibrationCore",
					"CinematicCamera",
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
                {
					"LiveLinkComponents",
				}
			);
		}
	}
}
