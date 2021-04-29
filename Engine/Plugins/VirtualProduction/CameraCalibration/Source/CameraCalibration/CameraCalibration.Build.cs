// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CameraCalibration : ModuleRules
	{
		public CameraCalibration(ReadOnlyTargetRules Target) : base(Target)
		{
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
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
                {
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
