// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

public class NvCloth : ModuleRules
{
    protected virtual string IncRootDirectory { get { return ModuleDirectory; } }
    protected virtual string LibRootDirectory { get { return ModuleDirectory; } }

    protected virtual string PxSharedDir      { get { return Path.Combine(IncRootDirectory, "PxShared"); } }
    protected virtual string NvClothDir       { get { return Path.Combine(IncRootDirectory, "NvCloth"); } }
    protected virtual string NvClothLibDir_Default { get { return Path.Combine(LibRootDirectory, "Lib"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
    protected virtual string LibrarySuffix         { get { return LibraryMode.AsSuffix(); } }

    protected virtual string LibraryFormatString_Default { get { return null; } }

    public NvCloth(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_NVCLOTH=1");

		PublicSystemIncludePaths.AddRange(new string[]
		{
			Path.Combine(NvClothDir, "include"),
			Path.Combine(NvClothDir, "extensions/include"),
            Path.Combine(PxSharedDir, "include"),
            Path.Combine(PxSharedDir, "include", "filebuf"),
			Path.Combine(PxSharedDir, "include", "foundation"),
			Path.Combine(PxSharedDir, "include", "pvd"),
			Path.Combine(PxSharedDir, "include", "task"),
			Path.Combine(PxSharedDir, "src", "foundation", "include")
		});

		// List of default library names (unused unless LibraryFormatString is non-null)
		List<string> NvClothLibraries = new List<string>();
		NvClothLibraries.AddRange(new string[]
		{
            "NvCloth{0}"
        });

		string LibraryFormatString = LibraryFormatString_Default;
		string NvClothLibDir = NvClothLibDir_Default;

		// Libraries and DLLs for windows platform
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			NvClothLibDir = Path.Combine(NvClothLibDir, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            
            string[] StaticLibrariesX64 = new string[]
            {
                "NvCloth{0}_x64.lib"
            };

			string[] RuntimeDependenciesX64 =
			{
                "NvCloth{0}_x64.dll",
			};

            string[] DelayLoadDLLsX64 =
            {
                "NvCloth{0}_x64.dll",
            };

            foreach(string Lib in StaticLibrariesX64)
            {
                PublicAdditionalLibraries.Add(Path.Combine(NvClothLibDir, String.Format(Lib, LibrarySuffix)));
            }

            foreach(string DLL in DelayLoadDLLsX64)
            {
                PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
            }

			string NvClothBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win64/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach(string RuntimeDependency in RuntimeDependenciesX64)
			{
				string FileName = NvClothBinariesDir + String.Format(RuntimeDependency, LibrarySuffix);
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

            if(LibrarySuffix != "")
            {
                PublicDefinitions.Add("UE_NVCLOTH_SUFFIX=" + LibrarySuffix);
            }
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			NvClothLibDir = Path.Combine(NvClothLibDir, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

            string[] StaticLibrariesX64 = new string[]
            {
                "NvCloth{0}_x86.lib"
            };

            string[] RuntimeDependenciesX64 =
            {
                "NvCloth{0}_x86.dll",
            };

            string[] DelayLoadDLLsX64 =
            {
                "NvCloth{0}_x86.dll",
            };

            foreach (string Lib in StaticLibrariesX64)
            {
                PublicAdditionalLibraries.Add(Path.Combine(NvClothLibDir, String.Format(Lib, LibrarySuffix)));
            }

            foreach (string DLL in DelayLoadDLLsX64)
            {
                PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
            }

            string NvClothBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win32/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
            foreach (string RuntimeDependency in RuntimeDependenciesX64)
            {
                string FileName = NvClothBinariesDir + String.Format(RuntimeDependency, LibrarySuffix);
                RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
                RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
            }

            if (LibrarySuffix != "")
            {
                PublicDefinitions.Add("UE_NVCLOTH_SUFFIX=" + LibrarySuffix);
            }
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			NvClothLibDir = Path.Combine(NvClothLibDir, "Mac");
			LibraryFormatString = "lib{0}.a";

            NvClothLibraries.Clear();

            string[] DynamicLibrariesMac = new string[]
            {
                "/libNvCloth{0}.dylib",
            };

            string PhysXBinDir = Target.UEThirdPartyBinariesDirectory + "PhysX3/Mac";

            foreach(string Lib in DynamicLibrariesMac)
            {
                string LibraryPath = PhysXBinDir + String.Format(Lib, LibrarySuffix);
                PublicDelayLoadDLLs.Add(LibraryPath);
                RuntimeDependencies.Add(LibraryPath);
            }

            if(LibrarySuffix != "")
            {
                PublicDefinitions.Add("UE_NVCLOTH_SUFFIX=" + LibrarySuffix);
            }
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
            if (Target.Architecture != "arm-unknown-linux-gnueabihf")
            {
                NvClothLibraries.Add("NvCloth{0}");
				NvClothLibDir = Path.Combine(NvClothLibDir, "Linux", Target.Architecture);
				LibraryFormatString = "lib{0}.a";
            }
        }
        else if (Target.Platform == UnrealTargetPlatform.Switch)
        {
			NvClothLibDir = Path.Combine(NvClothLibDir, "Switch");

            NvClothLibraries.Add("NvCloth{0}");

            LibraryFormatString = "lib{0}.a";
        }
        else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			PublicDefinitions.Add("_XBOX_ONE=1");

			// This MUST be defined for XboxOne!
			PublicDefinitions.Add("PX_HAS_SECURE_STRCPY=1");

			NvClothLibDir = Path.Combine(NvClothLibDir, "XboxOne", "VS2015");

            NvClothLibraries.Add("NvCloth{0}");

			LibraryFormatString = "{0}.lib";
		}

		// Add the libraries needed (used for all platforms except Windows)
		if (LibraryFormatString != null)
		{
			foreach (string Lib in NvClothLibraries)
			{
				string ConfiguredLib = String.Format(Lib, LibrarySuffix);
				string FinalLib = String.Format(LibraryFormatString, ConfiguredLib);
				PublicAdditionalLibraries.Add(Path.Combine(NvClothLibDir, FinalLib));
			}
		}
	}
}
