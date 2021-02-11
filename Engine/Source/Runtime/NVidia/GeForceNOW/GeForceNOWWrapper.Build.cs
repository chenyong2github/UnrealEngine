// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;

public class GeForceNOWWrapper : ModuleRules
{
	public GeForceNOWWrapper(ReadOnlyTargetRules Target) : base(Target)
	{
        PrivateIncludePaths.AddRange(
            new string[] {
                "Runtime/NVidia/GeForceNOW/Private" // For PCH includes (because they don't work with relative paths, yet)
            })
		;

		String GFNPath = Target.UEThirdPartySourceDirectory + "NVIDIA/GeForceNOW/";
		PublicSystemIncludePaths.Add(GFNPath + "include");

		PublicDependencyModuleNames.AddRange(
			new string[] {
				"GeForceNOW"
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
			}
		);

		if (Target.Type != TargetRules.TargetType.Server
		&& Target.Configuration != UnrealTargetConfiguration.Unknown
		&& Target.Configuration != UnrealTargetConfiguration.Debug
		&& (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32))
		{
			PublicDefinitions.Add("NV_GEFORCENOW=1");
		}
        else
		{
			PublicDefinitions.Add("NV_GEFORCENOW=0");
		}
	}
}
