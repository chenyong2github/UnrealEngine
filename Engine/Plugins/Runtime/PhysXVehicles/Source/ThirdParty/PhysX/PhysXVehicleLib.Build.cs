// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class PhysXVehicleLib : ModuleRules
{
    protected virtual string LibRootDirectory { get { return Path.Combine(Target.UEThirdPartySourceDirectory, "PhysX3"); } }
	protected virtual string PhysXLibDir      { get { return Path.Combine(LibRootDirectory, "Lib"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
    protected virtual string LibrarySuffix         { get { return LibraryMode.AsSuffix(); } }

    public PhysXVehicleLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // Libraries and DLLs for windows platform
		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.HoloLens ||
            Target.Platform == UnrealTargetPlatform.Win32)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), string.Format("PhysX3Vehicle{0}_{1}.lib", LibrarySuffix, Target.WindowsPlatform.GetArchitectureSubpath())));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Mac", string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "ARMv7", string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "x86", string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "ARM64", string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", "x64", string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Linux", Target.Architecture, string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "IOS", string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "TVOS", string.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.HTML5)
        {
			string OptimizationSuffix = "";
			if (Target.bCompileForSize)
			{
				OptimizationSuffix = "_Oz";
			}
			else
			{
				if (Target.Configuration == UnrealTargetConfiguration.Development)
				{
					OptimizationSuffix = "_O2";
				}
				else if (Target.Configuration == UnrealTargetConfiguration.Shipping)
				{
					OptimizationSuffix = "_O3";
				}
			}
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "HTML5", "PhysX3Vehicle" + OptimizationSuffix + ".bc"));
        }
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "XboxOne", "VS2015", string.Format("PhysX3Vehicle{0}.lib", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Switch", "libPhysX3Vehicle" + LibrarySuffix + ".a"));
        }
	}
}
