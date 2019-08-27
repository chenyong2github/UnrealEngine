// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class PhysXVehicleLib : ModuleRules
{
    // PhysXLibraryMode, GetPhysXLibraryMode and GetPhysXLibrarySuffix duplicated from PhysX.Build.cs

    enum PhysXLibraryMode
    {
        Debug,
        Profile,
        Checked,
        Shipping
    }

    PhysXLibraryMode GetPhysXLibraryMode(UnrealTargetConfiguration Config)
    {
        switch (Config)
        {
            case UnrealTargetConfiguration.Debug:
                if (Target.bDebugBuildsActuallyUseDebugCRT)
                {
                    return PhysXLibraryMode.Debug;
                }
                else
                {
                    return PhysXLibraryMode.Checked;
                }
            case UnrealTargetConfiguration.Shipping:
				return PhysXLibraryMode.Shipping;
            case UnrealTargetConfiguration.Test:
                return PhysXLibraryMode.Profile;
            case UnrealTargetConfiguration.Development:
            case UnrealTargetConfiguration.DebugGame:
            case UnrealTargetConfiguration.Unknown:
            default:
                if (Target.bUseShippingPhysXLibraries)
                {
                    return PhysXLibraryMode.Shipping;
                }
                else if (Target.bUseCheckedPhysXLibraries)
                {
                    return PhysXLibraryMode.Checked;
                }
                else
                {
                    return PhysXLibraryMode.Profile;
                }
        }
    }

    static string GetPhysXLibrarySuffix(PhysXLibraryMode Mode)
    {
        switch (Mode)
        {
            case PhysXLibraryMode.Debug:
                return "DEBUG";
            case PhysXLibraryMode.Checked:
                return "CHECKED";
            case PhysXLibraryMode.Profile:
                return "PROFILE";
            case PhysXLibraryMode.Shipping:
            default:
                return "";
        }
    }

    public PhysXVehicleLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // Determine which kind of libraries to link against
        PhysXLibraryMode LibraryMode = GetPhysXLibraryMode(Target.Configuration);
        string LibrarySuffix = GetPhysXLibrarySuffix(LibraryMode);

        string PhysXLibDir = Target.UEThirdPartySourceDirectory + "PhysX3/Lib/";

        // Libraries and DLLs for windows platform
        string LibraryDir;
		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.HoloLens ||
            Target.Platform == UnrealTargetPlatform.Win32)
        {
			LibraryDir = PhysXLibDir + Target.Platform.ToString() + "/VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName();

            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("PhysX3Vehicle{0}_{1}.lib", LibrarySuffix, Target.WindowsPlatform.GetArchitectureSubpath())));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            LibraryDir = PhysXLibDir + "Mac";

            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "ARM64", String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "ARMv7", String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "x64", String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "x86", String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
        {
            LibraryDir = PhysXLibDir + "Linux/" + Target.Architecture;

            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            LibraryDir = PhysXLibDir + "IOS";

            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            LibraryDir = PhysXLibDir + "TVOS";

            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.HTML5)
        {
			string OpimizationSuffix = "";
			if (Target.bCompileForSize)
			{
				OpimizationSuffix = "_Oz";
			}
			else
			{
				if (Target.Configuration == UnrealTargetConfiguration.Development)
				{
					OpimizationSuffix = "_O2";
				}
				else if (Target.Configuration == UnrealTargetConfiguration.Shipping)
				{
					OpimizationSuffix = "_O3";
				}
			}
            PublicAdditionalLibraries.Add(PhysXLibDir + "HTML5/PhysX3Vehicle" + OpimizationSuffix + ".bc");
        }
        else if (Target.Platform == UnrealTargetPlatform.PS4)
        {
            LibraryDir = PhysXLibDir + "PS4";

            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            LibraryDir = Path.Combine(PhysXLibDir, "XboxOne\\VS2015");

            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("PhysX3Vehicle{0}.lib", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            LibraryDir = PhysXLibDir + "Switch";
            PublicAdditionalLibraries.Add(Path.Combine(LibraryDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
    }
}
