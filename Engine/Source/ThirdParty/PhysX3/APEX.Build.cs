// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;
using System.Collections.Generic;

public class APEX : ModuleRules
{
    protected virtual List<string> ApexLibraries_Default { get { return new List<string>(); } }

    protected virtual bool bIsApexStaticallyLinked_Default { get { return false; } }
	protected virtual bool bHasApexLegacy_Default          { get { return true; } }
	protected virtual string LibraryFormatString_Default   { get { return null; } }

    protected virtual string IncRootDirectory { get { return ModuleDirectory; } }
    protected virtual string LibRootDirectory { get { return ModuleDirectory; } }

    protected virtual string ApexVersion { get { return "APEX_1.4"; } }
    protected virtual string ApexDir     { get { return Path.Combine(IncRootDirectory, ApexVersion); } }
    protected virtual string ApexLibDir_Default { get { return Path.Combine(LibRootDirectory, "Lib"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
    protected virtual string LibrarySuffix         { get { return LibraryMode.AsSuffix(); } }

    public APEX(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.AddRange(new string[]
		{
			Path.Combine(ApexDir, "include"),
			Path.Combine(ApexDir, "include", "clothing"),
			Path.Combine(ApexDir, "include", "nvparameterized"),
			Path.Combine(ApexDir, "include", "legacy"),
			Path.Combine(ApexDir, "include", "PhysX3"),
			Path.Combine(ApexDir, "common", "include"),
			Path.Combine(ApexDir, "common", "include", "autogen"),
			Path.Combine(ApexDir, "framework", "include"),
			Path.Combine(ApexDir, "framework", "include", "autogen"),
			Path.Combine(ApexDir, "shared", "general", "RenderDebug", "public"),
			Path.Combine(ApexDir, "shared", "general", "PairFilter", "include"),
			Path.Combine(ApexDir, "shared", "internal", "include"),
		});

		// List of default library names (unused unless LibraryFormatString is non-null)
		List<string> ApexLibraries = ApexLibraries_Default;
		ApexLibraries.AddRange(
			new string[]
			{
				"ApexCommon{0}",
				"ApexFramework{0}",
				"ApexShared{0}",
				"APEX_Clothing{0}",
			});
		string LibraryFormatString = LibraryFormatString_Default;

		bool bIsApexStaticallyLinked = bIsApexStaticallyLinked_Default;
		bool bHasApexLegacy = bHasApexLegacy_Default;

		string ApexLibDir = ApexLibDir_Default;

		// Libraries and DLLs for windows platform
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			ApexLibDir = Path.Combine(ApexLibDir, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			PublicAdditionalLibraries.Add(Path.Combine(ApexLibDir, String.Format("APEXFramework{0}_x64.lib", LibrarySuffix)));
			PublicDelayLoadDLLs.Add(String.Format("APEXFramework{0}_x64.dll", LibrarySuffix));

			string[] RuntimeDependenciesX64 =
			{
				"APEX_Clothing{0}_x64.dll",
				"APEX_Legacy{0}_x64.dll",
				"ApexFramework{0}_x64.dll",
			};

			string ApexBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win64/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string RuntimeDependency in RuntimeDependenciesX64)
			{
				string FileName = ApexBinariesDir + String.Format(RuntimeDependency, LibrarySuffix);
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}
			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_APEX_SUFFIX=" + LibrarySuffix);
			}

		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			ApexLibDir = Path.Combine(ApexLibDir, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			PublicAdditionalLibraries.Add(Path.Combine(ApexLibDir, String.Format("APEXFramework{0}_x86.lib", LibrarySuffix)));
			PublicDelayLoadDLLs.Add(String.Format("APEXFramework{0}_x86.dll", LibrarySuffix));

			string[] RuntimeDependenciesX86 =
			{
				"APEX_Clothing{0}_x86.dll",
				"APEX_Legacy{0}_x86.dll",
				"ApexFramework{0}_x86.dll",
			};

			string ApexBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win32/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string RuntimeDependency in RuntimeDependenciesX86)
			{
				string FileName = ApexBinariesDir + String.Format(RuntimeDependency, LibrarySuffix);
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}
			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_APEX_SUFFIX=" + LibrarySuffix);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
            string Arch = Target.WindowsPlatform.GetArchitectureSubpath();

			ApexLibDir = Path.Combine(ApexLibDir, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			PublicAdditionalLibraries.Add(Path.Combine(ApexLibDir, String.Format("APEXFramework{0}_{1}.lib", LibrarySuffix, Arch)));
			PublicDelayLoadDLLs.Add(String.Format("APEXFramework{0}_{1}.dll", LibrarySuffix, Arch));


            string[] RuntimeDependenciesTempl =
			{
                "APEX_Clothing{0}_{1}.dll",
                "APEX_Legacy{0}_{1}.dll",
                "ApexFramework{0}_{1}.dll",
			};

			string ApexBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/{1}/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.Platform.ToString());
			bHasApexLegacy = Target.Platform != UnrealTargetPlatform.HoloLens;

			foreach(string RuntimeDependency in RuntimeDependenciesTempl)
			{
				string FileName = ApexBinariesDir + String.Format(RuntimeDependency, LibrarySuffix, Arch);
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}
			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_APEX_SUFFIX=" + LibrarySuffix);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			ApexLibraries.Clear();
			ApexLibraries.AddRange(
				new string[]
				{
					"ApexCommon{0}",
					"ApexShared{0}",
				});

			ApexLibDir = Path.Combine(ApexLibDir, "Mac");
			LibraryFormatString = "lib{0}.a";

			string[] DynamicLibrariesMac = new string[] {
				"/libAPEX_Clothing{0}.dylib",
				"/libAPEX_Legacy{0}.dylib",
				"/libApexFramework{0}.dylib"
			};

			string PhysXBinariesDir = Target.UEThirdPartyBinariesDirectory + "PhysX3/Mac";
			foreach (string Lib in DynamicLibrariesMac)
			{
				string LibraryPath = PhysXBinariesDir + String.Format(Lib, LibrarySuffix);
				PublicDelayLoadDLLs.Add(LibraryPath);
				RuntimeDependencies.Add(LibraryPath);
			}
			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_APEX_SUFFIX=" + LibrarySuffix);
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			if (Target.Architecture.StartsWith("x86_64"))
			{
				ApexLibraries.Clear();
				string PhysXBinariesDir = Target.UEThirdPartyBinariesDirectory + "PhysX3/Linux/" + Target.Architecture;
				PrivateRuntimeLibraryPaths.Add(PhysXBinariesDir);

				string[] DynamicLibrariesLinux =
				{
					"/libApexCommon{0}.so",
					"/libApexFramework{0}.so",
					"/libApexShared{0}.so",
					"/libAPEX_Legacy{0}.so",
					"/libAPEX_Clothing{0}.so",
					"/libNvParameterized{0}.so",
					"/libRenderDebug{0}.so"
				};

				foreach (string RuntimeDependency in DynamicLibrariesLinux)
				{
					string LibraryPath = PhysXBinariesDir + String.Format(RuntimeDependency, LibrarySuffix);
					PublicAdditionalLibraries.Add(LibraryPath);
					RuntimeDependencies.Add(LibraryPath);
				}
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			bIsApexStaticallyLinked = true;
			bHasApexLegacy = false;

			PublicDefinitions.Add("_XBOX_ONE=1");

			// This MUST be defined for XboxOne!
			PublicDefinitions.Add("PX_HAS_SECURE_STRCPY=1");

			ApexLibDir = Path.Combine(ApexLibDir, "XboxOne", "VS2015");

			ApexLibraries.Add("NvParameterized{0}");
			ApexLibraries.Add("RenderDebug{0}");

			LibraryFormatString = "{0}.lib";
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			bIsApexStaticallyLinked = true;
			bHasApexLegacy = false;

			ApexLibDir = Path.Combine(ApexLibDir, "Switch");

			ApexLibraries.Add("NvParameterized{0}");
			ApexLibraries.Add("RenderDebug{0}");

			LibraryFormatString = "lib{0}.a";
		}

		PublicDefinitions.Add("APEX_UE4=1");

		PublicDefinitions.Add(string.Format("APEX_STATICALLY_LINKED={0}", bIsApexStaticallyLinked ? 1 : 0));
		PublicDefinitions.Add(string.Format("WITH_APEX_LEGACY={0}", bHasApexLegacy ? 1 : 0));

		// Add the libraries needed (used for all platforms except Windows)
		if (LibraryFormatString != null)
		{
			foreach (string Lib in ApexLibraries)
			{
				string ConfiguredLib = String.Format(Lib, LibrarySuffix);
				string FinalLib = String.Format(LibraryFormatString, ConfiguredLib);
				PublicAdditionalLibraries.Add(Path.Combine(ApexLibDir, FinalLib));
			}
		}		
	}
}
