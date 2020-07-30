// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class RigLogicLib : ModuleRules
{
    public string ModulePath
    {
        get { return ModuleDirectory; }
    }

    public RigLogicLib(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                    "Core",
                    "CoreUObject",
                    "Engine",
                    "ControlRig"
            }
        );

        if (Target.Type == TargetType.Editor)
        {
            PublicDependencyModuleNames.Add("UnrealEd");
        }

        Type = ModuleType.CPlusPlus;

        if (Target.LinkType != TargetLinkType.Monolithic)
        {
            PrivateDefinitions.Add("RL_BUILD_SHARED=1");
        }

        if (Target.Platform == UnrealTargetPlatform.Win32 ||
                Target.Platform == UnrealTargetPlatform.Win64 ||
                Target.Platform == UnrealTargetPlatform.Linux ||
                Target.Platform == UnrealTargetPlatform.Mac ||
                Target.Platform == UnrealTargetPlatform.PS4 ||
                Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            PrivateDefinitions.Add("RL_BUILD_WITH_SSE=1");
        }
    }
}
