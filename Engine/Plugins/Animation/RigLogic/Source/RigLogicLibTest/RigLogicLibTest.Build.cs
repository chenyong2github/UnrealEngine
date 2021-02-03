// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;
using Tools.DotNETCommon;

public class RigLogicLibTest : ModuleRules
{
    public RigLogicLibTest(ReadOnlyTargetRules Target) : base(Target)
    {
        if (Target.Platform == UnrealTargetPlatform.Win32 ||
            Target.Platform == UnrealTargetPlatform.Win64 ||
            Target.Platform == UnrealTargetPlatform.Linux ||
            Target.Platform == UnrealTargetPlatform.Mac)
        {
            PrivateDefinitions.Add("RL_BUILD_WITH_SSE=1");
        }

        string RigLogicLibPath = Path.GetFullPath(Path.Combine(ModuleDirectory, "../RigLogicLib"));

        if (Target.LinkType == TargetLinkType.Monolithic)
        {
            PublicDependencyModuleNames.Add("RigLogicLib");
            PrivateIncludePaths.Add(Path.Combine(RigLogicLibPath, "Private"));
        }
        else
        {
            PrivateDefinitions.Add("RIGLOGIC_MODULE_DISCARD");
            ConditionalAddModuleDirectory(new DirectoryReference(RigLogicLibPath));
        }

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "GoogleTest"
            }
        );

        if (Target.Platform == UnrealTargetPlatform.Win32 || Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            PublicDefinitions.Add("GTEST_OS_WINDOWS=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicDefinitions.Add("GTEST_OS_MAC=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS || Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PublicDefinitions.Add("GTEST_OS_IOS=1");
        }
        else if (Target.Platform == UnrealTargetPlatform.Android || Target.Platform == UnrealTargetPlatform.Lumin)
        {
            PublicDefinitions.Add("GTEST_OS_LINUX_ANDROID=1");
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix) || Target.Platform == UnrealTargetPlatform.PS4)
        {
            PublicDefinitions.Add("GTEST_OS_LINUX=1");
        }
    }
}
