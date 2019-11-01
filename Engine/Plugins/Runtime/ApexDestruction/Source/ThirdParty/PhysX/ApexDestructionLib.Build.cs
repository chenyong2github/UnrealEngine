// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

public class ApexDestructionLib : ModuleRules
{	
	protected virtual string LibraryFormatString_Default
    {
		get { return null; }
    }

	protected virtual string IncRootDirectory { get { return Path.Combine(Target.UEThirdPartySourceDirectory, "PhysX3"); } }
	protected virtual string LibRootDirectory { get { return Path.Combine(Target.UEThirdPartySourceDirectory, "PhysX3"); } }

    protected virtual string ApexVersion { get { return "APEX_1.4"; } }
    protected virtual string ApexDir     { get { return Path.Combine(IncRootDirectory, ApexVersion); } }
    protected virtual string ApexLibDir  { get { return Path.Combine(LibRootDirectory, "Lib"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
    protected virtual string LibrarySuffix         { get { return LibraryMode.AsSuffix(); } }

    public ApexDestructionLib(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        if (Target.bCompileAPEX == false)
        {
            return;
        }

        PublicSystemIncludePaths.AddRange(new string[]
		{
            Path.Combine(ApexDir, "include", "destructible"),
        });

        // List of default library names (unused unless LibraryFormatString is non-null)
        List<string> ApexLibraries = new List<string>();
        ApexLibraries.AddRange(new string[]
            {
                "APEX_Destructible{0}",
            });
        string LibraryFormatString = LibraryFormatString_Default;

        // Libraries and DLLs for windows platform
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicAdditionalLibraries.Add(Path.Combine(ApexLibDir, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), String.Format("APEXFramework{0}_x64.lib", LibrarySuffix)));
            PublicDelayLoadDLLs.Add(String.Format("APEXFramework{0}_x64.dll", LibrarySuffix));

            string[] RuntimeDependenciesX64 =
            {
                "APEX_Destructible{0}_x64.dll",
            };

            string ApexBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win64/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            foreach (string RuntimeDependency in RuntimeDependenciesX64)
            {
                string FileName = ApexBinariesDir + String.Format(RuntimeDependency, LibrarySuffix);
                RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
                RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
            }

        }
        else if (Target.Platform == UnrealTargetPlatform.Win32)
        {
            PublicAdditionalLibraries.Add(Path.Combine(ApexLibDir, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), String.Format("APEXFramework{0}_x86.lib", LibrarySuffix)));
            PublicDelayLoadDLLs.Add(String.Format("APEXFramework{0}_x86.dll", LibrarySuffix));

            string[] RuntimeDependenciesX86 =
            {
                "APEX_Destructible{0}_x86.dll",
            };

            string ApexBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win32/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            foreach (string RuntimeDependency in RuntimeDependenciesX86)
            {
                string FileName = ApexBinariesDir + String.Format(RuntimeDependency, LibrarySuffix);
                RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
                RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.HoloLens)
        {
            string Arch = Target.WindowsPlatform.GetArchitectureSubpath();

            PublicAdditionalLibraries.Add(Path.Combine(ApexLibDir, "HoloLens", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), String.Format("APEXFramework{0}_{1}.lib", LibrarySuffix, Arch)));
            PublicDelayLoadDLLs.Add(String.Format("APEXFramework{0}_{1}.dll", LibrarySuffix, Arch));

            string[] RuntimeDependenciesT =
            {
                "APEX_Destructible{0}_{1}.dll",
            };

            string ApexBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/HoloLens/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            foreach (string RuntimeDependency in RuntimeDependenciesT)
            {
                string FileName = ApexBinariesDir + String.Format(RuntimeDependency, LibrarySuffix, Arch);
                RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
                RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
            }

        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            string[] DynamicLibrariesMac = new string[] {
                "/libAPEX_Destructible{0}.dylib"
            };

            string PhysXBinariesDir = Target.UEThirdPartyBinariesDirectory + "PhysX3/Mac";
            foreach (string Lib in DynamicLibrariesMac)
            {
                string LibraryPath = PhysXBinariesDir + String.Format(Lib, LibrarySuffix);
                PublicDelayLoadDLLs.Add(LibraryPath);
                RuntimeDependencies.Add(LibraryPath);
            }
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
        {
            if (Target.Architecture.StartsWith("x86_64"))
            {
				string PhysXBinariesDir = Target.UEThirdPartyBinariesDirectory + "PhysX3/Linux/" + Target.Architecture;
				string LibraryPath = PhysXBinariesDir + String.Format("/libAPEX_Destructible{0}.so", LibrarySuffix);
				PublicAdditionalLibraries.Add(LibraryPath);
				RuntimeDependencies.Add(LibraryPath);
			}
        }
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
        {
			LibraryFormatString = Path.Combine(ApexLibDir, "XboxOne", "VS2015", "{0}.lib");
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
			LibraryFormatString = Path.Combine(ApexLibDir, "Switch", "lib{0}.a");
        }
		
		// Add the libraries needed (used for all platforms except Windows and Mac)
		if (LibraryFormatString != null)
        {
            foreach (string Lib in ApexLibraries)
            {
                string ConfiguredLib = String.Format(Lib, LibrarySuffix);
                string FinalLib = String.Format(LibraryFormatString, ConfiguredLib);
                PublicAdditionalLibraries.Add(FinalLib);
            }
        }
    }
}
