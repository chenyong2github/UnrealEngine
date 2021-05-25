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
					"CinematicCamera",
					"Core",
					"CoreUObject",
					"DeveloperSettings",
					"Engine",
					"RenderCore",
					"RHI",
					"Slate",
					"SlateCore",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
                {
					"LiveLinkComponents",
                    "Projects",
				}
			);

			if (Target.bBuildEditor == true)
			{
				PrivateDependencyModuleNames.Add("UnrealEd");
			}

			PrivateIncludePaths.AddRange(
				new string[] 
				{
            		//required for ScreenPass
            		"../../../../Source/Runtime/Renderer/Private",
            	}
			);
		}
	}
}
