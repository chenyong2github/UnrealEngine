// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class HAPMedia : ModuleRules
	{
		public HAPMedia(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[] {
					"Core",
                    "Projects",
					"WmfMediaFactory",
					"WmfMedia",
                });

            if (Target.Platform == UnrealTargetPlatform.Win64)
            {
                PrivateDependencyModuleNames.AddRange(
                    new string[] {
                    "HAPLib",
                    "SnappyLib",
                    });

                PublicDelayLoadDLLs.Add("mf.dll");
				PublicDelayLoadDLLs.Add("mfplat.dll");
				PublicDelayLoadDLLs.Add("mfplay.dll");
				PublicDelayLoadDLLs.Add("shlwapi.dll");

                PublicAdditionalLibraries.Add("mf.lib");
                PublicAdditionalLibraries.Add("mfplat.lib");
                PublicAdditionalLibraries.Add("mfuuid.lib");
                PublicAdditionalLibraries.Add("shlwapi.lib");
                PublicAdditionalLibraries.Add("d3d11.lib");
                PublicAdditionalLibraries.Add("d3dcompiler.lib");
            }
        }
	}
}
