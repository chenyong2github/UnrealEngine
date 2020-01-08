// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public class PhysXVehicleLib : ModuleRules
{
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string PhysXLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", "Lib"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
    protected virtual string LibrarySuffix         { get { return LibraryMode.AsSuffix(); } }

    public PhysXVehicleLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        // Libraries and DLLs for windows platform
        string LibDir = Path.Combine(PhysXLibDir, Target.Platform.ToString());

		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.HoloLens ||
            Target.Platform == UnrealTargetPlatform.Win32)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), String.Format("PhysX3Vehicle{0}_{1}.lib", LibrarySuffix, Target.WindowsPlatform.GetArchitectureSubpath())));
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
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
			PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Linux", Target.Architecture, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.IOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.TVOS)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
		}
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, "VS2015", String.Format("PhysX3Vehicle{0}.lib", LibrarySuffix)));
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
            PublicAdditionalLibraries.Add(Path.Combine(LibDir, String.Format("libPhysX3Vehicle{0}.a", LibrarySuffix)));
        }
	}
}
