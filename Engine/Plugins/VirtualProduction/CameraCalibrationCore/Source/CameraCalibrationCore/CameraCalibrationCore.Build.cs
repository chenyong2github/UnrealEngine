// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class CameraCalibrationCore : ModuleRules
	{
		public CameraCalibrationCore(ReadOnlyTargetRules Target) : base(Target)
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
					"Slate",
					"SlateCore",
					"ProceduralMeshComponent",
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
