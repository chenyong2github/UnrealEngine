// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class SteamVRInput : ModuleRules
{
	public SteamVRInput(ReadOnlyTargetRules Target) : base(Target)
    {
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
        PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
        PublicDependencyModuleNames.AddRange(
			new string[]
            {
                "Core",
                "CoreUObject",
                "Projects",
                "OpenVR",
                "SteamVR"
            }
			);

        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }

        RuntimeDependencies.Add("$(ProjectDir)/Config/SteamVRBindings/...");
    }
}
