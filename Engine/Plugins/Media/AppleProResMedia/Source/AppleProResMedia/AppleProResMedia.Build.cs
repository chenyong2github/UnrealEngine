// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class AppleProResMedia : ModuleRules
	{
		public AppleProResMedia(ReadOnlyTargetRules Target) : base(Target)
		{
            PublicDependencyModuleNames.AddRange(
				new string[] {
                    "ProResLib",
					"ProResToolbox",
                    "WmfMediaFactory",
                    "WmfMedia",
                });

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
                    "CoreUObject",
                    "Engine",
                    "MovieSceneCapture",
                    "Projects",
                    "WmfMediaFactory",
                    "WmfMedia"
                }
            );

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                PublicDelayLoadDLLs.Add("mf.dll");
                PublicDelayLoadDLLs.Add("mfplat.dll");
                PublicDelayLoadDLLs.Add("mfplay.dll");
                PublicDelayLoadDLLs.Add("shlwapi.dll");

                PublicAdditionalLibraries.Add("mf.lib");
                PublicAdditionalLibraries.Add("mfplat.lib");
                PublicAdditionalLibraries.Add("mfuuid.lib");
                PublicAdditionalLibraries.Add("shlwapi.lib");
                PublicAdditionalLibraries.Add("d3d11.lib");
            }
        }
    }
}
