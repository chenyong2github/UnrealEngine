// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System;
using System.IO;

public enum PhysXLibraryMode
{
	Debug,
	Profile,
	Checked,
	Shipping
}

public static class PhysXBuildExtensions
{
	public static PhysXLibraryMode GetPhysXLibraryMode(this ReadOnlyTargetRules Target)
	{
		switch (Target.Configuration)
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

	public static string AsSuffix(this PhysXLibraryMode Mode)
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
}

public class PhysX : ModuleRules
{
	protected virtual string PhysXVersion			{ get { return "PhysX_3.4"; } }
	protected virtual string PxSharedVersion		{ get { return "PxShared"; } }

	protected virtual string IncRootDirectory		{ get { return ModuleDirectory; } }
	protected virtual string LibRootDirectory		{ get { return ModuleDirectory; } }
	
	protected virtual string PhysXLibDir			{ get { return Path.Combine(LibRootDirectory, "Lib"); } }
	protected virtual string PxSharedLibDir			{ get { return Path.Combine(LibRootDirectory, "Lib"); } }
	protected virtual string PhysXIncludeDir		{ get { return Path.Combine(IncRootDirectory, PhysXVersion, "Include"); } }
	protected virtual string PxSharedIncludeDir		{ get { return Path.Combine(IncRootDirectory, PxSharedVersion, "include"); } }

	protected virtual PhysXLibraryMode LibraryMode	{ get { return Target.GetPhysXLibraryMode(); } }
	protected virtual string LibrarySuffix			{ get { return LibraryMode.AsSuffix(); } }

	public PhysX(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (LibraryMode == PhysXLibraryMode.Shipping)
		{
			PublicDefinitions.Add("WITH_PHYSX_RELEASE=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_PHYSX_RELEASE=0");
		}

		PublicSystemIncludePaths.AddRange(new string[]
		{
			PxSharedIncludeDir,
			Path.Combine(PxSharedIncludeDir, "cudamanager"),
			Path.Combine(PxSharedIncludeDir, "filebuf"),
			Path.Combine(PxSharedIncludeDir, "foundation"),
			Path.Combine(PxSharedIncludeDir, "pvd"),
			Path.Combine(PxSharedIncludeDir, "task"),
			PhysXIncludeDir,
			Path.Combine(PhysXIncludeDir, "cooking"),
			Path.Combine(PhysXIncludeDir, "common"),
			Path.Combine(PhysXIncludeDir, "extensions"),
			Path.Combine(PhysXIncludeDir, "geometry")
		});

		// Libraries and DLLs for windows platform
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			string[] StaticLibrariesX64 = new string[] {
				"PhysX3{0}_x64.lib",
				"PhysX3Extensions{0}_x64.lib",
				"PhysX3Cooking{0}_x64.lib",
				"PhysX3Common{0}_x64.lib",
				"PsFastXml{0}_x64.lib",
				"PxFoundation{0}_x64.lib",
				"PxPvdSDK{0}_x64.lib",
				"PxTask{0}_x64.lib",
			};

			string[] DelayLoadDLLsX64 = new string[] {
				"PxFoundation{0}_x64.dll",
				"PxPvdSDK{0}_x64.dll",
				"PhysX3{0}_x64.dll",
				"PhysX3Cooking{0}_x64.dll",
				"PhysX3Common{0}_x64.dll",
			};

			string[] PxSharedRuntimeDependenciesX64 = new string[] {
				"PxFoundation{0}_x64.dll",
				"PxPvdSDK{0}_x64.dll",
			};

			foreach (string Lib in StaticLibrariesX64)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, String.Format(Lib, LibrarySuffix)));
			}

			foreach (string DLL in DelayLoadDLLsX64)
			{
				PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
			}

			string PhysXBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win64/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string DLL in DelayLoadDLLsX64)
			{
				string FileName = PhysXBinariesDir + String.Format(DLL, LibrarySuffix);
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_PHYSX_SUFFIX=" + LibrarySuffix);
			}

			string PxSharedBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win64/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string DLL in PxSharedRuntimeDependenciesX64)
			{
				RuntimeDependencies.Add(PxSharedBinariesDir + String.Format(DLL, LibrarySuffix));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			string[] StaticLibrariesX86 = new string[] {
				"PhysX3{0}_x86.lib",
				"PhysX3Extensions{0}_x86.lib",
				"PhysX3Cooking{0}_x86.lib",
				"PhysX3Common{0}_x86.lib",
				"PsFastXml{0}_x86.lib",
				"PxFoundation{0}_x86.lib",
				"PxPvdSDK{0}_x86.lib",
				"PxTask{0}_x86.lib",
			};

			string[] DelayLoadDLLsX86 = new string[] {
				"PxFoundation{0}_x86.dll",
				"PxPvdSDK{0}_x86.dll",
				"PhysX3{0}_x86.dll",
				"PhysX3Cooking{0}_x86.dll",
				"PhysX3Common{0}_x86.dll"
			};

			foreach (string Lib in StaticLibrariesX86)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, String.Format(Lib, LibrarySuffix)));
			}

			foreach (string DLL in DelayLoadDLLsX86)
			{
				PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
			}

			string PhysXBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/Win32/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string DLL in DelayLoadDLLsX86)
			{
				string FileName = PhysXBinariesDir + String.Format(DLL, LibrarySuffix);
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_PHYSX_SUFFIX=" + LibrarySuffix);
			}

		}
		else if (Target.Platform == UnrealTargetPlatform.HoloLens)
		{
            string Arch = Target.WindowsPlatform.GetArchitectureSubpath();

			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());

			string[] StaticLibraries = new string[] {
				"PhysX3{0}_{1}.lib",
                "PhysX3Extensions{0}_{1}.lib",
                "PhysX3Cooking{0}_{1}.lib",
                "PhysX3Common{0}_{1}.lib",
                "PsFastXml{0}_{1}.lib",
                "PxFoundation{0}_{1}.lib",
                "PxPvdSDK{0}_{1}.lib",
                "PxTask{0}_{1}.lib",
			};

			string[] DelayLoadDLLs = new string[] {
                "PxFoundation{0}_{1}.dll",
                "PxPvdSDK{0}_{1}.dll",
                "PhysX3{0}_{1}.dll",
                "PhysX3Cooking{0}_{1}.dll",
                "PhysX3Common{0}_{1}.dll",
			};

			string[] PxSharedRuntimeDependencies = new string[] {
                "PxFoundation{0}_{1}.dll",
                "PxPvdSDK{0}_{1}.dll",
			};

			foreach (string Lib in StaticLibraries)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, String.Format(Lib, LibrarySuffix, Arch)));
			}

			foreach (string DLL in DelayLoadDLLs)
			{
				PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix, Arch));
			}
			string PhysXBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/{1}/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.Platform.ToString());
			foreach (string DLL in DelayLoadDLLs)
			{
				string FileName = PhysXBinariesDir + String.Format(DLL, LibrarySuffix, Arch);
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_PHYSX_SUFFIX=" + LibrarySuffix);
			}

			string PxSharedBinariesDir = String.Format("$(EngineDir)/Binaries/ThirdParty/PhysX3/{1}/VS{0}/", Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), Target.Platform.ToString());
			foreach (string DLL in PxSharedRuntimeDependencies)
			{
				RuntimeDependencies.Add(PxSharedBinariesDir + String.Format(DLL, LibrarySuffix, Arch));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string PhysXLibDirMac = Path.Combine(PhysXLibDir, "Mac");
			string PxSharedLibDirMac = Path.Combine(PxSharedLibDir, "Mac");

			string[] StaticLibrariesMac = new string[] {
				PhysXLibDirMac + "/libLowLevel{0}.a",
				PhysXLibDirMac + "/libLowLevelCloth{0}.a",
				PhysXLibDirMac + "/libPhysX3Extensions{0}.a",
				PhysXLibDirMac + "/libSceneQuery{0}.a",
				PhysXLibDirMac + "/libSimulationController{0}.a",
				PxSharedLibDirMac + "/libPxTask{0}.a",
				PxSharedLibDirMac + "/libPsFastXml{0}.a"
			};

			foreach (string Lib in StaticLibrariesMac)
			{
				PublicAdditionalLibraries.Add(String.Format(Lib, LibrarySuffix));
			}

			string[] DynamicLibrariesMac = new string[] {
				"/libPhysX3{0}.dylib",
				"/libPhysX3Cooking{0}.dylib",
				"/libPhysX3Common{0}.dylib",
				"/libPxFoundation{0}.dylib",
				"/libPxPvdSDK{0}.dylib",
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
				PublicDefinitions.Add("UE_PHYSX_SUFFIX=" + LibrarySuffix);
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Android))
		{
			string[] Architectures = new string[] {
				"ARMv7",
				"ARM64",
				"x86",
				"x64",
			};

			string[] StaticLibrariesAndroid = new string[] {
				"PhysX3{0}",
				"PhysX3Extensions{0}",
				"PhysX3Cooking{0}", // not needed until Apex
				"PhysX3Common{0}",
				//"PhysXVisualDebuggerSDK{0}",
				"PxFoundation{0}",
				"PxPvdSDK{0}",
				"PsFastXml{0}"
			};

			//if you are shipping, and you actually want the shipping libs, you do not need this lib
			if (!(LibraryMode == PhysXLibraryMode.Shipping && Target.bUseShippingPhysXLibraries))
			{
//				PublicAdditionalLibraries.Add("nvToolsExt");
			}

			foreach (string Architecture in Architectures)
			{
				foreach (string Lib in StaticLibrariesAndroid)
				{
					PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Android", Architecture,  "lib" + String.Format(Lib, LibrarySuffix) + ".a"));
				}
			}
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Unix))
		{
			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, "Linux", Target.Architecture);

			PublicSystemLibraries.Add("rt");

			string[] StaticLibrariesPhysXLinux = new string[] {
				"PhysX3{0}",
				"PhysX3Extensions{0}",
				"PhysX3Cooking{0}",
				"PhysX3Common{0}",
				"PxFoundation{0}",
				"PxPvdSDK{0}",
				"PsFastXml{0}"
			};

			foreach (string Lib in StaticLibrariesPhysXLinux)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, "lib" + String.Format(Lib, LibrarySuffix) + ".a"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, "IOS");

			string[] PhysXLibs = new string[]
				{
					"LowLevel{0}",
					"LowLevelAABB{0}",
					"LowLevelCloth{0}",
					"LowLevelDynamics{0}",
					"LowLevelParticles{0}",
					"PhysX3{0}",
					"PhysX3Common{0}",
					// "PhysX3Cooking{0}", // not needed until Apex
					"PhysX3Extensions{0}",
					"SceneQuery{0}",
					"SimulationController{0}",
					"PxFoundation{0}",
					"PxTask{0}",
					"PxPvdSDK{0}",
					"PsFastXml{0}"
				};

			foreach (string PhysXLib in PhysXLibs)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, "lib" + String.Format(PhysXLib, LibrarySuffix) + ".a"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, "TVOS");

			string[] PhysXLibs = new string[]
				{
					"LowLevel{0}",
					"LowLevelAABB{0}",
					"LowLevelCloth{0}",
					"LowLevelDynamics{0}",
					"LowLevelParticles{0}",
					"PhysX3{0}",
					"PhysX3Common{0}",
					// "PhysX3Cooking{0}", // not needed until Apex
					"PhysX3Extensions{0}",
					"SceneQuery{0}",
					"SimulationController{0}",
					"PxFoundation{0}",
					"PxTask{0}",
					"PxPvdSDK{0}",
					"PsFastXml"
				};

			foreach (string PhysXLib in PhysXLibs)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, "lib" + String.Format(PhysXLib, LibrarySuffix) + ".a"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.HTML5)
		{
			string[] PhysXLibs = new string[]
			{
				"LowLevel",
				"LowLevelAABB",
				"LowLevelCloth",
				"LowLevelDynamics",
				"LowLevelParticles",
				"PhysX3",
				"PhysX3CharacterKinematic",
				"PhysX3Common",
				"PhysX3Cooking",
				"PhysX3Extensions",
				//"PhysXVisualDebuggerSDK",
				"SceneQuery",
				"SimulationController",
				"PxFoundation",
				"PxTask",
				"PxPvdSDK",
				"PsFastXml"
			};

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

			foreach (var lib in PhysXLibs)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "HTML5", lib + OptimizationSuffix + ".bc"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.XboxOne)
		{
			PublicDefinitions.Add("PX_PHYSX_STATIC_LIB=1");
			PublicDefinitions.Add("_XBOX_ONE=1");

			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, "XboxOne", "VS2015");

			string[] StaticLibrariesXB1 = new string[] {
				"PhysX3{0}.lib",
				"PhysX3Extensions{0}.lib",
				"PhysX3Cooking{0}.lib",
				"PhysX3Common{0}.lib",
				"LowLevel{0}.lib",
				"LowLevelAABB{0}.lib",
				"LowLevelCloth{0}.lib",
				"LowLevelDynamics{0}.lib",
				"LowLevelParticles{0}.lib",
				"SceneQuery{0}.lib",
				"SimulationController{0}.lib",
				"PxFoundation{0}.lib",
				"PxTask{0}.lib",
				"PxPvdSDK{0}.lib",
				"PsFastXml{0}.lib"
			};

			foreach (string Lib in StaticLibrariesXB1)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, String.Format(Lib, LibrarySuffix)));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Switch)
		{
			string PlatformPhysXLibDir = Path.Combine(PhysXLibDir, "Switch");

			string[] StaticLibrariesSwitch = new string[] {
					"LowLevel{0}",
					"LowLevelAABB{0}",
					"LowLevelCloth{0}",
					"LowLevelDynamics{0}",
					"LowLevelParticles{0}",
					"PhysX3{0}",
					"PhysX3Common{0}",
					"PhysX3Cooking{0}",
					"PhysX3Extensions{0}",
					"SceneQuery{0}",
					"SimulationController{0}",
					"PxFoundation{0}",
					"PxTask{0}",
					"PxPvdSDK{0}",
					"PsFastXml{0}"
			};

			foreach (string Lib in StaticLibrariesSwitch)
			{
				PublicAdditionalLibraries.Add(Path.Combine(PlatformPhysXLibDir, "lib" + String.Format(Lib, LibrarySuffix) + ".a"));
			}
		}
	}
}
