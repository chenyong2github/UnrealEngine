// Copyright Epic Games, Inc. All Rights Reserved.

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
	protected virtual string PhysXVersion { get { return "PhysX_3.4"; } }
	protected virtual string PxSharedVersion { get { return "PxShared"; } }

	protected virtual string IncRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }
	protected virtual string LibRootDirectory { get { return Target.UEThirdPartySourceDirectory; } }

	protected virtual string PhysXLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", "Lib"); } }
	protected virtual string PxSharedLibDir { get { return Path.Combine(LibRootDirectory, "PhysX3", "Lib"); } }
	protected virtual string PhysXIncludeDir { get { return Path.Combine(IncRootDirectory, "PhysX3", PhysXVersion, "Include"); } }
	protected virtual string PxSharedIncludeDir { get { return Path.Combine(IncRootDirectory, "PhysX3", PxSharedVersion, "include"); } }

	protected virtual PhysXLibraryMode LibraryMode { get { return Target.GetPhysXLibraryMode(); } }
	protected virtual string LibrarySuffix { get { return LibraryMode.AsSuffix(); } }

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

		string EngineBinThirdPartyPath = Path.Combine("$(EngineDir)", "Binaries", "ThirdParty", "PhysX3");

		// Libraries and DLLs for windows platform
		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Platform != UnrealTargetPlatform.Win32)
		{
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
				PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), String.Format(Lib, LibrarySuffix)));
			}

			foreach (string DLL in DelayLoadDLLsX64)
			{
				PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
			}

			string PhysXBinariesDir = Path.Combine(EngineBinThirdPartyPath, "Win64", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string DLL in DelayLoadDLLsX64)
			{
				string FileName = Path.Combine(PhysXBinariesDir, String.Format(DLL, LibrarySuffix));
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_PHYSX_SUFFIX=" + LibrarySuffix);
			}

			foreach (string DLL in PxSharedRuntimeDependenciesX64)
			{
				RuntimeDependencies.Add(Path.Combine(PhysXBinariesDir, String.Format(DLL, LibrarySuffix)));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Win32)
		{
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
				PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), String.Format(Lib, LibrarySuffix)));
			}

			foreach (string DLL in DelayLoadDLLsX86)
			{
				PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix));
			}

			string PhysXBinariesDir = Path.Combine(EngineBinThirdPartyPath, "Win32", "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string DLL in DelayLoadDLLsX86)
			{
				string FileName = Path.Combine(PhysXBinariesDir, String.Format(DLL, LibrarySuffix));
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
				PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName(), String.Format(Lib, LibrarySuffix, Arch)));
			}

			foreach (string DLL in DelayLoadDLLs)
			{
				PublicDelayLoadDLLs.Add(String.Format(DLL, LibrarySuffix, Arch));
			}
			string PhysXBinariesDir = Path.Combine(EngineBinThirdPartyPath, Target.Platform.ToString(), "VS" + Target.WindowsPlatform.GetVisualStudioCompilerVersionName());
			foreach (string DLL in DelayLoadDLLs)
			{
				string FileName = Path.Combine(PhysXBinariesDir, String.Format(DLL, LibrarySuffix, Arch));
				RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
				RuntimeDependencies.Add(Path.ChangeExtension(FileName, ".pdb"), StagedFileType.DebugNonUFS);
			}

			if (LibrarySuffix != "")
			{
				PublicDefinitions.Add("UE_PHYSX_SUFFIX=" + LibrarySuffix);
			}

			foreach (string DLL in PxSharedRuntimeDependencies)
			{
				RuntimeDependencies.Add(Path.Combine(PhysXBinariesDir, String.Format(DLL, LibrarySuffix, Arch)));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string[] StaticLibrariesMac = new string[] {
				Path.Combine(PhysXLibDir, "Mac", "libLowLevel{0}.a"),
				Path.Combine(PhysXLibDir, "Mac", "libLowLevelCloth{0}.a"),
				Path.Combine(PhysXLibDir, "Mac", "libPhysX3Extensions{0}.a"),
				Path.Combine(PhysXLibDir, "Mac", "libSceneQuery{0}.a"),
				Path.Combine(PhysXLibDir, "Mac", "libSimulationController{0}.a"),
				Path.Combine(PxSharedLibDir, "Mac", "libPxTask{0}.a"),
				Path.Combine(PxSharedLibDir, "Mac", "libPsFastXml{0}.a")
			};

			foreach (string Lib in StaticLibrariesMac)
			{
				PublicAdditionalLibraries.Add(String.Format(Lib, LibrarySuffix));
			}

			string[] DynamicLibrariesMac = new string[] {
				"libPhysX3{0}.dylib",
				"libPhysX3Cooking{0}.dylib",
				"libPhysX3Common{0}.dylib",
				"libPxFoundation{0}.dylib",
				"libPxPvdSDK{0}.dylib",
			};

			string PhysXBinariesDir = Path.Combine(Target.UEThirdPartyBinariesDirectory, "PhysX3", "Mac");
			foreach (string Lib in DynamicLibrariesMac)
			{
				string LibraryPath = Path.Combine(PhysXBinariesDir, String.Format(Lib, LibrarySuffix));
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
				PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "Linux", Target.Architecture, "lib" + String.Format(Lib, LibrarySuffix) + ".a"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.IOS)
		{
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
				PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "IOS", "lib" + String.Format(PhysXLib, LibrarySuffix) + ".a"));
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.TVOS)
		{
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
				PublicAdditionalLibraries.Add(Path.Combine(PhysXLibDir, "TVOS", "lib" + String.Format(PhysXLib, LibrarySuffix) + ".a"));
			}
		}
	}
}
