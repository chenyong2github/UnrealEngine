// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

public class NvCloth : ModuleRules
{
	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string PxSharedDir { get { return Path.Combine(IncRootDirectory, "PhysX3", "PxShared"); } }
	protected virtual string NvClothDir { get { return Path.Combine(IncRootDirectory, "PhysX3", "NvCloth"); } }
	protected virtual string NvClothLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", "Lib"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
	protected virtual string LibrarySuffix { get { return LibraryMode.AsSuffix(); } }

	protected virtual string LibraryFormatString_Default { get { return null; } }

	public NvCloth(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicDefinitions.Add("WITH_NVCLOTH=1");

		PublicSystemIncludePaths.AddRange(new string[]
		{
			Path.Combine(NvClothDir, "include"),
			Path.Combine(NvClothDir, "extensions", "include"),
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

		string LibraryFormatString = LibraryFormatString_Default; // used with all platform except windows

		string EngineBinThirdPartyPath = Path.Combine("$(EngineDir)", "Binaries", "ThirdParty", "PhysX3");
		string LibDir;

		// Libraries and DLLs for windows platform
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			LibDir = Path.Combine(NvClothLibDir, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

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
                PublicAdditionalLibraries.Add(Path.Combine(LibDir, String.Format(Lib, LibrarySuffix)));
			}

			foreach (string DLL in DelayLoadDLLsX64)
			{
                PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
			}

			string NvClothBinariesDir = Path.Combine(EngineBinThirdPartyPath, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string RuntimeDependency in RuntimeDependenciesX64)
			{
				string FileName = Path.Combine(NvClothBinariesDir, String.Format(RuntimeDependency, LibrarySuffix));
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_NVCLOTH_SUFFIX=" + LibrarySuffix);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			LibDir = Path.Combine(NvClothLibDir, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

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
                PublicAdditionalLibraries.Add(Path.Combine(LibDir, String.Format(Lib, LibrarySuffix)));
            }

			foreach (string DLL in DelayLoadDLLsX64)
			{
				PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
			}

			string NvClothBinariesDir = Path.Combine(EngineBinThirdPartyPath, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string RuntimeDependency in RuntimeDependenciesX64)
			{
				string FileName = Path.Combine(NvClothBinariesDir, String.Format(RuntimeDependency, LibrarySuffix));
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_NVCLOTH_SUFFIX=" + LibrarySuffix);
			}
		}

		// the following platforms uses "LibraryFormatString" at the end of this object

		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			LibraryFormatString = Path.Combine("Mac", "lib{0}.a");

			NvClothLibraries.Clear();

			string[] DynamicLibrariesMac = new string[]
			{
				"libNvCloth{0}.dylib",
			};

			string PhysXBinDir = Path.Combine(Target.UEThirdPartyBinariesDirectory, "PhysX3", "Mac");

			foreach (string Lib in DynamicLibrariesMac)
			{
				string LibraryPath = Path.Combine(PhysXBinDir, String.Format(Lib, LibrarySuffix));
				PublicDelayLoadDLLs.Add(LibraryPath);
				RuntimeDependencies.Add(LibraryPath);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_NVCLOTH_SUFFIX=" + LibrarySuffix);
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Architecture != "arm-unknown-linux-gnueabihf")
			{
				NvClothLibraries.Add("NvCloth{0}");
				LibraryFormatString = Path.Combine("Linux", Target.Architecture, "lib{0}.a");
			}
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
