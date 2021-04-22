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
					"RHI",
					"Engine",
					"RenderCore",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
                {
                    "Projects",
				}
			);
			
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
