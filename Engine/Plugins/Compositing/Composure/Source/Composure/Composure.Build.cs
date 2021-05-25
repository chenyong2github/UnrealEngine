// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class Composure : ModuleRules
	{
		public Composure(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.AddRange(
				new string[] {
                    "../../../../Source/Runtime/Engine/",
					"Composure/Private/"
				}
				);
            
            PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"CameraCalibrationCore",
					"CinematicCamera",
					"Core",
                    "CoreUObject",
                    "Engine",
					"MediaIOCore",
					"MovieScene",
					"MovieSceneTracks",
					"OpenColorIO",
					"TimeManagement",
                }
				);

            PrivateDependencyModuleNames.AddRange(
                new string[]
                {
					// Removed dependency, until the MediaFrameworkUtilities plugin is available for all platforms
                    //"MediaFrameworkUtilities",

					"ImageWriteQueue",
					"MediaAssets",
					"MovieSceneCapture",
					"RHI",
				}
				);

            if (Target.bBuildEditor == true)
            {
                PrivateDependencyModuleNames.AddRange(
					new string[]
                    {
						"ActorLayerUtilities",
						"EditorStyle",
						"Slate",
						"SlateCore",
						"UnrealEd",
					}
					);
            }
        }
    }
}
