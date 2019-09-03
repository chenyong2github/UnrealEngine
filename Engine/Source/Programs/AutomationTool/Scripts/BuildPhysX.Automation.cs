// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using System.Linq;
using System.Reflection;
using Microsoft.Win32;
using System.Diagnostics;
using System.Text.RegularExpressions;
using AutomationTool;
using UnrealBuildTool;
using Tools.DotNETCommon;

[Help("Builds PhysX/APEX libraries using CMake build system.")]
[Help("TargetLibs", "Specify a list of target libraries to build, separated by '+' characters (eg. -TargetLibs=PhysX+APEX). Default is PhysX+APEX.")]
[Help("TargetPlatforms", "Specify a list of target platforms to build, separated by '+' characters (eg. -TargetPlatforms=Win32+Win64). Architectures are specified with '-'. Default is Win32+Win64+PS4.")]
[Help("TargetConfigs", "Specify a list of configurations to build, separated by '+' characters (eg. -TargetConfigs=profile+debug). Default is profile+release+checked.")]
[Help("TargetWindowsCompilers", "Specify a list of target compilers to use when building for Windows, separated by '+' characters (eg. -TargetCompilers=VisualStudio2012+VisualStudio2015). Default is VisualStudio2015.")]
[Help("SkipBuild", "Do not perform build step. If this argument is not supplied libraries will be built (in accordance with TargetLibs, TargetPlatforms and TargetWindowsCompilers).")]
[Help("SkipDeployLibs", "Do not perform library deployment to the engine. If this argument is not supplied libraries will be copied into the engine.")]
[Help("SkipDeploySource", "Do not perform source deployment to the engine. If this argument is not supplied source will be copied into the engine.")]
[Help("SkipCreateChangelist", "Do not create a P4 changelist for source or libs. If this argument is not supplied source and libs will be added to a Perforce changelist.")]
[Help("SkipSubmit", "Do not perform P4 submit of source or libs. If this argument is not supplied source and libs will be automatically submitted to Perforce. If SkipCreateChangelist is specified, this argument applies by default.")]
[Help("Robomerge", "Which robomerge action to apply to the submission. If we're skipping submit, this is not used.")]
[RequireP4]
class BuildPhysX : BuildCommand
{
	const int InvalidChangeList = -1;

	// The libs we can optionally build
	private enum PhysXTargetLib
	{
		PhysX,
		APEX,		// Note: Building APEX deploys shared binaries and libs
        NvCloth
	}

	private struct TargetPlatformData
	{
		public UnrealTargetPlatform Platform;
		public string Architecture;

		public TargetPlatformData(UnrealTargetPlatform InPlatform)
		{
			Platform = InPlatform;

			Architecture = "";

			// Linux never has an empty architecture.
			if (Platform == UnrealTargetPlatform.Linux)
			{
				Architecture = "x86_64-unknown-linux-gnu";
			}
			else if (Platform == UnrealTargetPlatform.Linux)
			{
				Architecture = "aarch64-unknown-linux-gnueabi";
			}
		}
		public TargetPlatformData(UnrealTargetPlatform InPlatform, string InArchitecture)
		{
			Platform = InPlatform;
			Architecture = InArchitecture;
		}

		public override string ToString()
		{
			return Architecture == "" ? Platform.ToString() : Platform.ToString() + "_" + Architecture;
		}
	}

	// Apex libs that do not have an APEX prefix in their name
	private static string[] APEXSpecialLibs = { "NvParameterized", "RenderDebug" };

	// We cache our own MSDev and MSBuild executables
	private static FileReference MsDev14Exe;
	private static FileReference MsBuildExe;

	// Cache directories under the PhysX/ directory
	private static DirectoryReference PhysXSourceRootDirectory = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine", "Source", "ThirdParty", "PhysX3");
	private static DirectoryReference PhysX34SourceRootDirectory = DirectoryReference.Combine(PhysXSourceRootDirectory, "PhysX_3.4");
	private static DirectoryReference APEX14SourceRootDirectory = DirectoryReference.Combine(PhysXSourceRootDirectory, "APEX_1.4");
    private static DirectoryReference NvClothSourceRootDirectory = DirectoryReference.Combine(PhysXSourceRootDirectory, "NvCloth");
	private static DirectoryReference SharedSourceRootDirectory = DirectoryReference.Combine(PhysXSourceRootDirectory, "PxShared");
	private static DirectoryReference RootOutputBinaryDirectory = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine", "Binaries", "ThirdParty", "PhysX3");
	private static DirectoryReference RootOutputLibDirectory = DirectoryReference.Combine(PhysXSourceRootDirectory, "Lib");
	private static DirectoryReference ThirdPartySourceDirectory = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine", "Source", "ThirdParty");

	private static DirectoryReference DumpSymsPath = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine", "Binaries", "Linux", "dump_syms");
	private static DirectoryReference BreakpadSymbolEncoderPath = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine", "Binaries", "Linux", "BreakpadSymbolEncoder");

	//private static DirectoryReference PhysX34SourceLibRootDirectory = DirectoryReference.Combine(PhysX34SourceRootDirectory, "Lib");
	//private static DirectoryReference APEX14SourceLibRootDirectory = DirectoryReference.Combine(APEX14SourceRootDirectory, "Lib");
	//private static DirectoryReference SharedSourceLibRootDirectory = DirectoryReference.Combine(SharedSourceRootDirectory, "Lib");


	//private static DirectoryReference PhysXEngineBinaryRootDirectory = DirectoryReference.Combine(UnrealBuildTool.UnrealBuildTool.RootDirectory, "Engine\\Binaries\\ThirdParty\\PhysX3");
	//private static DirectoryReference PhysX34EngineBinaryRootDirectory = DirectoryReference.Combine(PhysXEngineBinaryRootDirectory, "PhysX-3.4");
	//private static DirectoryReference APEX14EngineBinaryRootDirectory = DirectoryReference.Combine(PhysXEngineBinaryRootDirectory, "APEX-1.4");
	//private static DirectoryReference SharedEngineBinaryRootDirectory = DirectoryReference.Combine(PhysXEngineBinaryRootDirectory, "PxShared-1.0");

	private static string GetCMakeNameAndSetupEnv(TargetPlatformData TargetData)
	{
		DirectoryReference CMakeRootDirectory = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine", "Extras", "ThirdPartyNotUE", "CMake");
		if(BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Linux)
		{
			return "cmake";
		}

		Environment.SetEnvironmentVariable("CMAKE_ROOT", DirectoryReference.Combine(CMakeRootDirectory, "share").ToString());
		LogInformation("set {0}={1}", "CMAKE_ROOT", Environment.GetEnvironmentVariable("CMAKE_ROOT"));

		if (TargetData.Platform == UnrealTargetPlatform.HTML5)
		{
			return "cmake";
		}
		if (TargetData.Platform == UnrealTargetPlatform.Mac ||
			TargetData.Platform == UnrealTargetPlatform.IOS ||
			TargetData.Platform == UnrealTargetPlatform.TVOS)
		{
			return FileReference.Combine(CMakeRootDirectory, "bin", "cmake").ToString();
		}
		return FileReference.Combine(CMakeRootDirectory, "bin", "cmake.exe").ToString();
	}

	private static string GetCMakeTargetDirectoryName(TargetPlatformData TargetData, WindowsCompiler TargetWindowsCompiler)
	{
		string VisualStudioDirectoryName;
		switch (TargetWindowsCompiler)
		{
			case WindowsCompiler.VisualStudio2015_DEPRECATED:
				VisualStudioDirectoryName = "VS2015";
				break;
			default:
				throw new AutomationException(String.Format("Non-CMake or unsupported windows compiler '{0}' supplied to GetCMakeTargetDirectoryName", TargetWindowsCompiler));
		}

		// Note slashes need to be '/' as this gets string-composed in the CMake script with other paths
		if (TargetData.Platform == UnrealTargetPlatform.Win32)
		{
			return "Win32/" + VisualStudioDirectoryName;
		}
		if (TargetData.Platform == UnrealTargetPlatform.Win64)
		{
			return "Win64/" + VisualStudioDirectoryName;
		}
		if (TargetData.Platform ==  UnrealTargetPlatform.HoloLens)
		{
			return "HoloLens/" + VisualStudioDirectoryName;
		}
		if (TargetData.Platform == UnrealTargetPlatform.Android)
		{
			switch (TargetData.Architecture)
			{
				default:
				case "armv7": return "Android/ARMv7";
				case "arm64": return "Android/ARM64";
				case "x86": return "Android/x86";
				case "x64": return "Android/x64";
			}
		}
		return TargetData.Platform.ToString();
	}

	private static DirectoryReference GetProjectDirectory(PhysXTargetLib TargetLib, TargetPlatformData TargetData, WindowsCompiler TargetWindowsCompiler = WindowsCompiler.VisualStudio2015_DEPRECATED)
	{
		DirectoryReference Directory = new DirectoryReference(GetTargetLibRootDirectory(TargetLib).ToString());

		switch(TargetLib)
		{
			case PhysXTargetLib.PhysX:
				Directory = DirectoryReference.Combine(Directory, "Source");
				break;
			case PhysXTargetLib.APEX:
				// APEX has its 'compiler' directory in a different location off the root of APEX
				break;
		}

		return DirectoryReference.Combine(Directory, "compiler", GetCMakeTargetDirectoryName(TargetData, TargetWindowsCompiler));
	}

	private static string GetBundledLinuxLibCxxFlags()
	{
		string CxxFlags = "\"-I " + ThirdPartySourceDirectory + "/Linux/LibCxx/include -I " + ThirdPartySourceDirectory + "/Linux/LibCxx/include/c++/v1\"";
		string CxxLinkerFlags = "\"-stdlib=libc++ -nodefaultlibs -L " + ThirdPartySourceDirectory + "/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/ " + ThirdPartySourceDirectory + "/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++.a " + ThirdPartySourceDirectory + "/Linux/LibCxx/lib/Linux/x86_64-unknown-linux-gnu/libc++abi.a -lm -lc -lgcc_s\"";

		return "-DCMAKE_CXX_FLAGS=" + CxxFlags + " -DCMAKE_EXE_LINKER_FLAGS=" + CxxLinkerFlags + " -DCAMKE_MODULE_LINKER_FLAGS=" + CxxLinkerFlags + " -DCMAKE_SHARED_LINKER_FLAGS=" + CxxLinkerFlags + " ";
	}

	private static string GetLinuxToolchainSettings(TargetPlatformData TargetData)
	{
		if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Linux)
		{
			// in native builds we don't really use a crosstoolchain description, just use system compiler
			return " -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++";
		}

		// otherwise, use a per-architecture file.
		return " -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\Linux\\LinuxCrossToolchain.multiarch.cmake\"" + " -DARCHITECTURE_TRIPLE=" + TargetData.Architecture;
	}

	private static string GetCMakeArguments(PhysXTargetLib TargetLib, TargetPlatformData TargetData, string BuildConfig = "", WindowsCompiler TargetWindowsCompiler = WindowsCompiler.VisualStudio2015_DEPRECATED)
	{
		string VisualStudioName;
		switch(TargetWindowsCompiler)
		{
			case WindowsCompiler.VisualStudio2015_DEPRECATED:
				VisualStudioName = "Visual Studio 14 2015";
				break;
			default:
				throw new AutomationException(String.Format("Non-CMake or unsupported platform '{0}' supplied to GetCMakeArguments", TargetData.ToString()));
		}

		string OutputFlags = " -DPX_OUTPUT_LIB_DIR=\"" + GetPlatformLibDirectory(TargetData, TargetWindowsCompiler) + "\"";
		if(PlatformHasBinaries(TargetData))
		{
			OutputFlags += " -DPX_OUTPUT_DLL_DIR=" + GetPlatformBinaryDirectory(TargetData, TargetWindowsCompiler) + " -DPX_OUTPUT_EXE_DIR=" + GetPlatformBinaryDirectory(TargetData, TargetWindowsCompiler);
		}

		// Enable response files for platforms that require them.
		// Response files are used for include paths etc, to fix max command line length issues.
		if (TargetData.Platform == UnrealTargetPlatform.PS4 ||
			TargetData.Platform == UnrealTargetPlatform.Switch ||
			TargetData.Platform == UnrealTargetPlatform.Linux ||
			TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			OutputFlags += " -DUSE_RESPONSE_FILES=1";
		}

		string ApexFlags = " -DAPEX_ENABLE_UE4=1";
		switch (TargetLib)
		{
			case PhysXTargetLib.PhysX:
				DirectoryReference PhysXCMakeFiles = DirectoryReference.Combine(PhysX34SourceRootDirectory, "Source", "compiler", "cmake");
				if (TargetData.Platform == UnrealTargetPlatform.Win32)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -AWin32 -DTARGET_BUILD_PLATFORM=windows" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Win64)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -Ax64 -DTARGET_BUILD_PLATFORM=windows" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.HoloLens)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -A ARM64" + "-DTARGET_BUILD_PLATFORM=windows -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.PS4)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "PS4").ToString() + " -G \"Unix Makefiles\" -DTARGET_BUILD_PLATFORM=ps4 -DCMAKE_BUILD_TYPE=" + BuildConfig + " -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\ps4\\PS4Toolchain.txt\"" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.XboxOne)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "XboxOne").ToString() + " -G \"Visual Studio 14 2015\" -DTARGET_BUILD_PLATFORM=xboxone -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\xboxone\\XboxOneToolchain.txt\" -DCMAKE_GENERATOR_PLATFORM=Durango" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Android)
				{
					string NDKDirectory = Environment.GetEnvironmentVariable("NDKROOT");

					// don't register if we don't have an NDKROOT specified
					if (String.IsNullOrEmpty(NDKDirectory))
					{
						throw new AutomationException("NDKROOT is not specified; cannot build Android.");
					}

					NDKDirectory = NDKDirectory.Replace("\"", "");

					string AndroidAPILevel = "android-19";
					string AndroidABI = "armeabi-v7a";
					switch (TargetData.Architecture)
					{
						case "armv7": AndroidAPILevel = "android-19"; AndroidABI = "armeabi-v7a"; break;
						case "arm64": AndroidAPILevel = "android-21"; AndroidABI = "arm64-v8a"; break;
						case "x86":   AndroidAPILevel = "android-19"; AndroidABI = "x86"; break;
						case "x64":   AndroidAPILevel = "android-21"; AndroidABI = "x86_64"; break;
					}
					return DirectoryReference.Combine(PhysXCMakeFiles, "Android").ToString() + " -G \"MinGW Makefiles\" -DTARGET_BUILD_PLATFORM=android -DCMAKE_BUILD_TYPE=" + BuildConfig + " -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\android\\android.toolchain.cmake\" -DANDROID_NDK=\"" + NDKDirectory + "\" -DCMAKE_MAKE_PROGRAM=\"" + NDKDirectory + "\\prebuilt\\windows-x86_64\\bin\\make.exe\" -DANDROID_NATIVE_API_LEVEL=\"" + AndroidAPILevel + "\" -DANDROID_ABI=\"" + AndroidABI + "\" -DANDROID_STL=gnustl_shared" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "Linux").ToString() + " --no-warn-unused-cli -G \"Unix Makefiles\" -DTARGET_BUILD_PLATFORM=linux -DPX_STATIC_LIBRARIES=1 " + GetBundledLinuxLibCxxFlags() + " -DCMAKE_BUILD_TYPE=" + BuildConfig + GetLinuxToolchainSettings(TargetData) + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Mac)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "Mac").ToString() + " -G \"Xcode\" -DTARGET_BUILD_PLATFORM=mac" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.IOS)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "IOS").ToString() + " -G \"Xcode\" -DTARGET_BUILD_PLATFORM=ios" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.TVOS)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "TVOS").ToString() + " -G \"Xcode\" -DTARGET_BUILD_PLATFORM=tvos" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Switch)
				{
					return DirectoryReference.Combine(PhysXCMakeFiles, "Switch").ToString() + " -G \"Visual Studio 14 2015\" -DTARGET_BUILD_PLATFORM=switch -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\switch\\NX64Toolchain.txt\" -DCMAKE_GENERATOR_PLATFORM=NX-NXFP2-a64" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.HTML5)
				{
					string CmakeToolchainFile = FileReference.Combine(PhysXSourceRootDirectory, "Externals", "CMakeModules", "HTML5", "Emscripten." + BuildConfig + ".cmake").ToString();
					return "\"" + DirectoryReference.Combine(PhysXCMakeFiles, "HTML5").ToString() + "\"" +
						" -G \"Unix Makefiles\" -DTARGET_BUILD_PLATFORM=html5" +
						" -DPXSHARED_ROOT_DIR=\"" + SharedSourceRootDirectory.ToString() + "\"" +
						" -DNVTOOLSEXT_INCLUDE_DIRS=\"" + PhysX34SourceRootDirectory + "/externals/nvToolsExt/include\"" +
						" -DEMSCRIPTEN_GENERATE_BITCODE_STATIC_LIBRARIES=ON " +
						" -DCMAKE_BUILD_TYPE=\"Release\" -DCMAKE_TOOLCHAIN_FILE=\"" + CmakeToolchainFile + "\"" +
						OutputFlags;
				}
				throw new AutomationException(String.Format("Non-CMake or unsupported platform '{0}' supplied to GetCMakeArguments", TargetData.ToString()));
			case PhysXTargetLib.APEX:
				DirectoryReference ApexCMakeFiles = DirectoryReference.Combine(APEX14SourceRootDirectory, "compiler", "cmake");
				if (TargetData.Platform == UnrealTargetPlatform.Win32)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -AWin32 -DTARGET_BUILD_PLATFORM=windows" + OutputFlags + ApexFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Win64)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -Ax64 -DTARGET_BUILD_PLATFORM=windows" + OutputFlags + ApexFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.HoloLens)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -A ARM64" + " -DTARGET_BUILD_PLATFORM=windows -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0" + OutputFlags + ApexFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.PS4)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "PS4").ToString() + " -G \"Unix Makefiles\" -DTARGET_BUILD_PLATFORM=ps4 -DCMAKE_BUILD_TYPE=" + BuildConfig + " -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\ps4\\PS4Toolchain.txt\"" + OutputFlags + ApexFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.XboxOne)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "XboxOne").ToString() + " -G \"Visual Studio 14 2015\" -DTARGET_BUILD_PLATFORM=xboxone -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\xboxone\\XboxOneToolchain.txt\" -DCMAKE_GENERATOR_PLATFORM=DURANGO" + OutputFlags + ApexFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Switch)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "Switch").ToString() + " -G \"Visual Studio 14 2015\" -DTARGET_BUILD_PLATFORM=switch -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\switch\\NX64Toolchain.txt\" -DCMAKE_GENERATOR_PLATFORM=NX-NXFP2-a64" + OutputFlags + ApexFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "Linux").ToString() + " --no-warn-unused-cli -G \"Unix Makefiles\" -DTARGET_BUILD_PLATFORM=linux -DPX_STATIC_LIBRARIES=1 -DAPEX_LINUX_SHARED_LIBRARIES=1 " + GetBundledLinuxLibCxxFlags() + " -DCMAKE_BUILD_TYPE=" + BuildConfig + GetLinuxToolchainSettings(TargetData) + OutputFlags + ApexFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Mac)
				{
					return DirectoryReference.Combine(ApexCMakeFiles, "Mac").ToString() + " -G \"Xcode\" -DTARGET_BUILD_PLATFORM=mac" + OutputFlags + ApexFlags;
				}
				throw new AutomationException(String.Format("Non-CMake or unsupported platform '{0}' supplied to GetCMakeArguments", TargetData.ToString()));
            case PhysXTargetLib.NvCloth:
                DirectoryReference NvClothCMakeFiles = DirectoryReference.Combine(NvClothSourceRootDirectory, "compiler", "cmake");
				if (TargetData.Platform == UnrealTargetPlatform.Win32)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -AWin32 -DTARGET_BUILD_PLATFORM=windows -DNV_CLOTH_ENABLE_CUDA=0 -DNV_CLOTH_ENABLE_DX11=0" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Win64)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -Ax64 -DTARGET_BUILD_PLATFORM=windows -DNV_CLOTH_ENABLE_CUDA=0 -DNV_CLOTH_ENABLE_DX11=0" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.HoloLens)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "Windows").ToString() + " -G \"" + VisualStudioName + "\" -A ARM64" + " -DTARGET_BUILD_PLATFORM=windows -DCMAKE_SYSTEM_NAME=WindowsStore -DCMAKE_SYSTEM_VERSION=10.0 -DNV_CLOTH_ENABLE_CUDA=0 -DNV_CLOTH_ENABLE_DX11=0" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.PS4)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "PS4").ToString() + " -G \"Unix Makefiles\" -DTARGET_BUILD_PLATFORM=ps4 -DCMAKE_BUILD_TYPE=" + BuildConfig + " -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\PS4\\PS4Toolchain.txt\"" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Switch)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "Switch").ToString() + " -G \"Visual Studio 14 2015\" -DTARGET_BUILD_PLATFORM=switch -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\switch\\NX64Toolchain.txt\" -DCMAKE_GENERATOR_PLATFORM=NX-NXFP2-a64" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.XboxOne)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "XboxOne").ToString() + " -G \"Visual Studio 14 2015\" -DTARGET_BUILD_PLATFORM=xboxone -DCMAKE_TOOLCHAIN_FILE=\"" + PhysXSourceRootDirectory + "\\Externals\\CMakeModules\\XboxOne\\XboxOneToolchain.txt\" -DCMAKE_GENERATOR_PLATFORM=DURANGO" + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "Linux").ToString() + " --no-warn-unused-cli -G \"Unix Makefiles\" -DTARGET_BUILD_PLATFORM=linux -DPX_STATIC_LIBRARIES=1 " + GetBundledLinuxLibCxxFlags() + " -DCMAKE_BUILD_TYPE=" + BuildConfig + GetLinuxToolchainSettings(TargetData) + OutputFlags;
				}
				if (TargetData.Platform == UnrealTargetPlatform.Mac)
				{
					return DirectoryReference.Combine(NvClothCMakeFiles, "Mac").ToString() + " -G \"Xcode\" -DTARGET_BUILD_PLATFORM=mac" + OutputFlags;
				}

				throw new AutomationException(String.Format("Non-CMake or unsupported platform '{0}' supplied to GetCMakeArguments", TargetData.ToString()));
			default:
				throw new AutomationException(String.Format("Non-CMake or unsupported lib '{0}' supplied to GetCMakeArguments", TargetLib));
		}
	}

	private static string GetMsDevExe(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Win32)
		{
			return MsDev14Exe.ToString();
		}
		if (TargetData.Platform == UnrealTargetPlatform.Win64)
		{
			return MsDev14Exe.ToString();
		}if (TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			return MsDev14Exe.ToString();
		}
		if (TargetData.Platform == UnrealTargetPlatform.XboxOne)
		{
			return MsDev14Exe.ToString();
		}
		if (TargetData.Platform == UnrealTargetPlatform.Switch)
		{
			return MsDev14Exe.ToString();
		}

		throw new AutomationException(String.Format("Non-MSBuild or unsupported platform '{0}' supplied to GetMsDevExe", TargetData.ToString()));
	}

	private static string GetMsBuildExe(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 ||
			TargetData.Platform == UnrealTargetPlatform.XboxOne || TargetData.Platform == UnrealTargetPlatform.Switch || 
			TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			return MsBuildExe.ToString();
		}

		throw new AutomationException(String.Format("Non-MSBuild or unsupported platform '{0}' supplied to GetMsBuildExe", TargetData.ToString()));
	}

	private static string GetTargetLibSolutionName(PhysXTargetLib TargetLib)
	{
		switch (TargetLib)
		{
			case PhysXTargetLib.PhysX:
				return "PhysX.sln";
			case PhysXTargetLib.APEX:
				return "APEX.sln";
            case PhysXTargetLib.NvCloth:
                return "NvCloth.sln";
			default:
				throw new AutomationException(String.Format("Unknown target lib '{0}' specified to GetTargetLibSolutionName", TargetLib));
		}
	}

	private static FileReference GetTargetLibSolutionFileName(PhysXTargetLib TargetLib, TargetPlatformData TargetData, WindowsCompiler TargetWindowsCompiler)
	{
		DirectoryReference Directory = GetProjectDirectory(TargetLib, TargetData, TargetWindowsCompiler);
		return FileReference.Combine(Directory, GetTargetLibSolutionName(TargetLib));
	}

	private static bool DoesPlatformUseMSBuild(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 ||
			TargetData.Platform == UnrealTargetPlatform.XboxOne || TargetData.Platform == UnrealTargetPlatform.Switch || 
			TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			return true;
		}

		return false;
	}

	private static bool DoesPlatformUseMakefiles(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Android || TargetData.Platform == UnrealTargetPlatform.Linux ||
			TargetData.Platform == UnrealTargetPlatform.LinuxAArch64 || TargetData.Platform == UnrealTargetPlatform.HTML5 ||
			TargetData.Platform == UnrealTargetPlatform.PS4)
		{
			return true;
		}

		return false;
	}

	private static bool DoesPlatformUseXcode(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Mac || TargetData.Platform == UnrealTargetPlatform.IOS ||
			TargetData.Platform == UnrealTargetPlatform.TVOS)
		{
			return true;
		}

		return false;
	}

	private static DirectoryReference GetTargetLibRootDirectory(PhysXTargetLib TargetLib)
	{
		switch (TargetLib)
		{
			case PhysXTargetLib.PhysX:
				return PhysX34SourceRootDirectory;
			case PhysXTargetLib.APEX:
				return APEX14SourceRootDirectory;
            case PhysXTargetLib.NvCloth:
                return NvClothSourceRootDirectory;
			default:
				throw new AutomationException(String.Format("Unknown target lib '{0}' specified to GetTargetLibRootDirectory", TargetLib));
		}
	}

	private List<TargetPlatformData> GetTargetPlatforms()
	{
		List<TargetPlatformData> TargetPlatforms = new List<TargetPlatformData>();

		// Remove any platforms that aren't enabled on the command line
		string TargetPlatformFilter = ParseParamValue("TargetPlatforms", "Win32+Win64+PS4+Switch+HoloLens");
		if (TargetPlatformFilter != null)
		{
			foreach (string TargetPlatformName in TargetPlatformFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
			{
				string[] TargetPlatformAndArch = TargetPlatformName.Split(new char[] { '-' }, StringSplitOptions.RemoveEmptyEntries);

				UnrealTargetPlatform TargetPlatform = UnrealTargetPlatform.Parse(TargetPlatformAndArch[0]);
				if (TargetPlatformAndArch.Count() == 2)
				{
					TargetPlatforms.Add(new TargetPlatformData(TargetPlatform, TargetPlatformAndArch[1]));
				}
				else if (TargetPlatformAndArch.Count() > 2)
				{
					// Linux archs are OS triplets, so have multiple dashes
					string DashedArch = TargetPlatformAndArch[1];
					for(int Idx = 2; Idx < TargetPlatformAndArch.Count(); ++Idx)
					{
						DashedArch += "-" + TargetPlatformAndArch[Idx];
					}
					TargetPlatforms.Add(new TargetPlatformData(TargetPlatform, DashedArch));
				}
				else
				{
					TargetPlatforms.Add(new TargetPlatformData(TargetPlatform));
				}
			}
		}

		return TargetPlatforms;
	}

	public List<string> GetTargetConfigurations()
	{
		List<string> TargetConfigs = new List<string>();
		// Remove any configs that aren't enabled on the command line
		string TargetConfigFilter = ParseParamValue("TargetConfigs", "profile+release+checked");
		if (TargetConfigFilter != null)
		{
			foreach(string TargetConfig in TargetConfigFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
			{
				TargetConfigs.Add(TargetConfig);
			}
		}

		return TargetConfigs;
	}

	private List<PhysXTargetLib> GetTargetLibs()
	{
		List<PhysXTargetLib> TargetLibs = new List<PhysXTargetLib>();
		string TargetLibsFilter = ParseParamValue("TargetLibs", "PhysX+APEX+NvCloth");
		if (TargetLibsFilter != null)
		{
			foreach (string TargetLibName in TargetLibsFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
			{
				PhysXTargetLib TargetLib;
				if (!Enum.TryParse(TargetLibName, out TargetLib))
				{
					throw new AutomationException(String.Format("Unknown target lib '{0}' specified on command line", TargetLibName));
				}
				else
				{
					TargetLibs.Add(TargetLib);
				}
			}
		}
		return TargetLibs;
	}

	private List<WindowsCompiler> GetTargetWindowsCompilers()
	{
		List<WindowsCompiler> TargetWindowsCompilers = new List<WindowsCompiler>();
		string TargetWindowsCompilersFilter = ParseParamValue("TargetWindowsCompilers", "VisualStudio2015");
		if (TargetWindowsCompilersFilter != null)
		{
			foreach (string TargetWindowsCompilerName in TargetWindowsCompilersFilter.Split(new char[] { '+' }, StringSplitOptions.RemoveEmptyEntries))
			{
				WindowsCompiler TargetWindowsCompiler;
				if (!Enum.TryParse(TargetWindowsCompilerName, out TargetWindowsCompiler))
				{
					throw new AutomationException(String.Format("Unknown target windows compiler '{0}' specified on command line", TargetWindowsCompilerName));
				}
				else
				{
					TargetWindowsCompilers.Add(TargetWindowsCompiler);
				}
			}
		}
		return TargetWindowsCompilers;
	}

	private static void MakeFreshDirectoryIfRequired(DirectoryReference Directory)
	{
		if (!DirectoryReference.Exists(Directory))
		{
			DirectoryReference.CreateDirectory(Directory);
		}
		else
		{

			InternalUtils.SafeDeleteDirectory(Directory.FullName);
			DirectoryReference.CreateDirectory(Directory);
		}
	}

	public static int RunLocalProcess(Process LocalProcess)
	{
		int ExitCode = -1;

		// release all process resources
		using (LocalProcess)
		{
			LocalProcess.StartInfo.UseShellExecute = false;
			LocalProcess.StartInfo.RedirectStandardOutput = true;

			try
			{
				// Start the process up and then wait for it to finish
				LocalProcess.Start();
				LocalProcess.BeginOutputReadLine();

				if (LocalProcess.StartInfo.RedirectStandardError)
				{
					LocalProcess.BeginErrorReadLine();
				}

				LocalProcess.WaitForExit();
				ExitCode = LocalProcess.ExitCode;
			}
			catch (Exception ex)
			{
				throw new AutomationException(ex, "Failed to start local process for action (\"{0}\"): {1} {2}", ex.Message, LocalProcess.StartInfo.FileName, LocalProcess.StartInfo.Arguments);
			}
		}

		return ExitCode;
	}

	public static int RunLocalProcessAndLogOutput(ProcessStartInfo StartInfo)
	{
		Process LocalProcess = new Process();
		LocalProcess.StartInfo = StartInfo;
		LocalProcess.OutputDataReceived += (Sender, Line) => { if (Line != null && Line.Data != null) Tools.DotNETCommon.Log.TraceInformation(Line.Data); };
		return RunLocalProcess(LocalProcess);
	}

	private static void SetupBuildForTargetLibAndPlatform(PhysXTargetLib TargetLib, TargetPlatformData TargetData, List<string> TargetConfigurations, List<WindowsCompiler> TargetWindowsCompilers, bool bCleanOnly)
	{
		// make sure we set up the environment variable specifying where the root of the PhysX SDK is
		Environment.SetEnvironmentVariable("GW_DEPS_ROOT", PhysXSourceRootDirectory.ToString());
		LogInformation("set {0}={1}", "GW_DEPS_ROOT", Environment.GetEnvironmentVariable("GW_DEPS_ROOT"));
		Environment.SetEnvironmentVariable("CMAKE_MODULE_PATH", DirectoryReference.Combine(PhysXSourceRootDirectory, "Externals", "CMakeModules").ToString());
		LogInformation("set {0}={1}", "CMAKE_MODULE_PATH", Environment.GetEnvironmentVariable("CMAKE_MODULE_PATH"));

		string CMakeName = GetCMakeNameAndSetupEnv(TargetData);

		if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 || 
			TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			// for windows platforms we support building against multiple compilers
			foreach (WindowsCompiler TargetWindowsCompiler in TargetWindowsCompilers)
			{
				DirectoryReference CMakeTargetDirectory = GetProjectDirectory(TargetLib, TargetData, TargetWindowsCompiler);
				MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

				if (!bCleanOnly)
				{
					LogInformation("Generating projects for lib " + TargetLib.ToString() + ", " + TargetData.ToString());

					ProcessStartInfo StartInfo = new ProcessStartInfo();
					StartInfo.FileName = CMakeName;
					StartInfo.WorkingDirectory = CMakeTargetDirectory.ToString();
					StartInfo.Arguments = GetCMakeArguments(TargetLib, TargetData, "", TargetWindowsCompiler);

					RunLocalProcessAndLogOutput(StartInfo);
				}
			}
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Switch)
		{
			DirectoryReference CMakeTargetDirectory = GetProjectDirectory(TargetLib, TargetData);
			MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

			if (!bCleanOnly)
			{
				LogInformation("Generating projects for lib " + TargetLib.ToString() + ", " + TargetData.ToString());

				ProcessStartInfo StartInfo = new ProcessStartInfo();
				StartInfo.FileName = CMakeName;
				StartInfo.WorkingDirectory = CMakeTargetDirectory.ToString();
				StartInfo.Arguments = GetCMakeArguments(TargetLib, TargetData, "");

				RunLocalProcessAndLogOutput(StartInfo);
			}
		}
		else if (TargetData.Platform == UnrealTargetPlatform.PS4 || TargetData.Platform == UnrealTargetPlatform.Android ||
			TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			foreach (string BuildConfig in TargetConfigurations)
			{
				DirectoryReference CMakeTargetDirectory = GetProjectDirectory(TargetLib, TargetData);
				CMakeTargetDirectory = DirectoryReference.Combine(CMakeTargetDirectory, BuildConfig);
				MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

				if (!bCleanOnly)
				{
					LogInformation("Generating projects for lib " + TargetLib.ToString() + ", " + TargetData.ToString());

					if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
					{
						// the libraries are broken when compiled with clang 7.0.1
						string OriginalToolchainPath = Environment.GetEnvironmentVariable("LINUX_MULTIARCH_ROOT");
						if (!string.IsNullOrEmpty(OriginalToolchainPath))
						{
							string ToolchainPathToUse = OriginalToolchainPath.Replace("v13_clang-7.0.1-centos7", "v12_clang-6.0.1-centos7");
							LogInformation("Working around problems with newer clangs: {0} -> {1}", OriginalToolchainPath, ToolchainPathToUse);
							Environment.SetEnvironmentVariable("LINUX_MULTIARCH_ROOT", ToolchainPathToUse);
						}
						else
						{
							LogWarning("LINUX_MULTIARCH_ROOT is not set!");
						}
					}

					ProcessStartInfo StartInfo = new ProcessStartInfo();
					StartInfo.FileName = CMakeName;
					StartInfo.WorkingDirectory = CMakeTargetDirectory.ToString();
					StartInfo.Arguments = GetCMakeArguments(TargetLib, TargetData, BuildConfig);

					System.Console.WriteLine("Working in '{0}'", StartInfo.WorkingDirectory);
					LogInformation("Working in '{0}'", StartInfo.WorkingDirectory);

					System.Console.WriteLine("{0} {1}", StartInfo.FileName, StartInfo.Arguments);
					LogInformation("{0} {1}", StartInfo.FileName, StartInfo.Arguments);

					if (RunLocalProcessAndLogOutput(StartInfo) != 0)
					{
						throw new AutomationException(String.Format("Unable to generate projects for {0}.", TargetLib.ToString() + ", " + TargetData.ToString()));
					}
				}
			}
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Mac || TargetData.Platform == UnrealTargetPlatform.IOS ||
			TargetData.Platform == UnrealTargetPlatform.TVOS)
		{
			DirectoryReference CMakeTargetDirectory = GetProjectDirectory(TargetLib, TargetData);
			MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

			if (!bCleanOnly)
			{
				LogInformation("Generating projects for lib " + TargetLib.ToString() + ", " + TargetData.ToString());

				ProcessStartInfo StartInfo = new ProcessStartInfo();
				StartInfo.FileName = CMakeName;
				StartInfo.WorkingDirectory = CMakeTargetDirectory.ToString();
				StartInfo.Arguments = GetCMakeArguments(TargetLib, TargetData);

				RunLocalProcessAndLogOutput(StartInfo);
			}
		}
		else if (TargetData.Platform == UnrealTargetPlatform.HTML5)
		{
			// NOTE: HTML5 does not do "debug" - the full text blows out memory
			//	   instead, HTML5 builds have 4 levels of optimizations
			// so, MAP BuildConfig to HTML5 optimization levels
			Dictionary<string, string> BuildMap = new Dictionary<string, string>()
			{
				{"debug", "-O0"},
				{"checked", "-O2"},
				{"profile", "-Oz"},
				{"release", "-O3"}
			};
			DirectoryReference HTML5CMakeModules = DirectoryReference.Combine(PhysXSourceRootDirectory, "Externals", "CMakeModules", "HTML5");
			MakeFreshDirectoryIfRequired(HTML5CMakeModules);

				foreach(string BuildConfig in TargetConfigurations)
			{
				DirectoryReference CMakeTargetDirectory = GetProjectDirectory(TargetLib, TargetData);
				CMakeTargetDirectory = DirectoryReference.Combine(CMakeTargetDirectory, "BUILD" + BuildMap[BuildConfig]);
				MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

				if (!bCleanOnly)
				{
					LogInformation("Generating projects for lib " + TargetLib.ToString() + ", " + TargetData.ToString());

					// CMAKE_TOOLCHAIN_FILE
					Environment.SetEnvironmentVariable("LIB_SUFFIX", GetConfigurationSuffix(BuildConfig, TargetData)); // only used in HTML5's CMakefiles

					string orig = File.ReadAllText(HTML5SDKInfo.EmscriptenCMakeToolChainFile);
					string txt = Regex.Replace(orig, "-O2" , BuildMap[BuildConfig] );
					string CmakeToolchainFile = FileReference.Combine(HTML5CMakeModules, "Emscripten." + BuildConfig + ".cmake").ToString();
					File.WriteAllText(CmakeToolchainFile, txt);

					// ----------------------------------------

					// CMAKE
					ProcessStartInfo StartInfo = new ProcessStartInfo();
					StartInfo.FileName = "python";
					StartInfo.WorkingDirectory = CMakeTargetDirectory.ToString();
					StartInfo.Arguments = "\"" + HTML5SDKInfo.EMSCRIPTEN_ROOT + "\\emcmake\" cmake " + GetCMakeArguments(TargetLib, TargetData, BuildConfig);

					LogInformation("Working in: {0}", StartInfo.WorkingDirectory);
					LogInformation("{0} {1}", StartInfo.FileName, StartInfo.Arguments);

					if (RunLocalProcessAndLogOutput(StartInfo) != 0)
					{
						throw new AutomationException(String.Format("Unabled to generate projects for {0}.", TargetLib.ToString() + ", " + TargetData.ToString()));
					}
				}
			}
		}
		else
		{
			DirectoryReference CMakeTargetDirectory = GetProjectDirectory(TargetLib, TargetData);
			MakeFreshDirectoryIfRequired(CMakeTargetDirectory);

			if (!bCleanOnly)
			{
				LogInformation("Generating projects for lib " + TargetLib.ToString() + ", " + TargetData.ToString());

				ProcessStartInfo StartInfo = new ProcessStartInfo();
				StartInfo.FileName = CMakeName;
				StartInfo.WorkingDirectory = CMakeTargetDirectory.ToString();
				StartInfo.Arguments = GetCMakeArguments(TargetLib, TargetData);

				RunLocalProcessAndLogOutput(StartInfo);
			}
		}
	}

	private static string GetMsDevExe(WindowsCompiler Version)
	{
		DirectoryReference VSPath;
		// It's not fatal if VS2013 isn't installed for VS2015 builds (for example, so don't crash here)
		if(WindowsExports.TryGetVSInstallDir(Version, out VSPath))
		{
			return FileReference.Combine(VSPath, "Common7", "IDE", "Devenv.com").FullName;
		}
		return null;
	}

	private static string GetMsBuildExe(WindowsCompiler Version)
	{
		string VisualStudioToolchainVersion = "";
		switch (Version)
		{
			case WindowsCompiler.VisualStudio2015_DEPRECATED:
				VisualStudioToolchainVersion = "14.0";
				break;
		}
		string ProgramFilesPath = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86);
		string MSBuildPath = Path.Combine(ProgramFilesPath, "MSBuild", VisualStudioToolchainVersion, "Bin", "MSBuild.exe");
		if (File.Exists(MSBuildPath))
		{
			return MSBuildPath;
		}
		return null;
	}

	private static string RemoveOtherMakeAndCygwinFromPath(string WindowsPath)
	{
		string[] PathComponents = WindowsPath.Split(';');
		string NewPath = "";
		foreach(string PathComponent in PathComponents)
		{
			// everything what contains /bin or /sbin is suspicious, check if it has make in it
			if (PathComponent.Contains("\\bin") || PathComponent.Contains("/bin") || PathComponent.Contains("\\sbin") || PathComponent.Contains("/sbin"))
			{
				if (File.Exists(PathComponent + "/make.exe") || File.Exists(PathComponent + "make.exe") || File.Exists(PathComponent + "/cygwin1.dll"))
				{
					// gotcha!
					LogInformation("Removing {0} from PATH since it contains possibly colliding make.exe", PathComponent);
					continue;
				}
			}

			NewPath = NewPath + ';' + PathComponent + ';';
		}

		return NewPath;
	}
	private static void SetupStaticBuildEnvironment()
	{
		if (!Utils.IsRunningOnMono)
		{
			string VS2015Path = GetMsDevExe(WindowsCompiler.VisualStudio2015_DEPRECATED);
			if (VS2015Path != null)
			{
				MsDev14Exe = new FileReference(GetMsDevExe(WindowsCompiler.VisualStudio2015_DEPRECATED));
				MsBuildExe = new FileReference(GetMsBuildExe(WindowsCompiler.VisualStudio2015_DEPRECATED));
			}

			// ================================================================================
			// ThirdPartyNotUE
			// NOTE: these are Windows executables
			if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			{
				DirectoryReference ThirdPartyNotUERootDirectory = DirectoryReference.Combine(CommandUtils.RootDirectory, "Engine", "Extras", "ThirdPartyNotUE");
				string CMakePath = DirectoryReference.Combine(ThirdPartyNotUERootDirectory, "CMake", "bin").ToString();
				string MakePath = DirectoryReference.Combine(ThirdPartyNotUERootDirectory, "GNU_Make", "make-3.81", "bin").ToString();

				string PrevPath = Environment.GetEnvironmentVariable("PATH");
				// mixing bundled make and cygwin make is no good. Try to detect and remove cygwin paths.
				string PathWithoutCygwin = RemoveOtherMakeAndCygwinFromPath(PrevPath);
				Environment.SetEnvironmentVariable("PATH", CMakePath + ";" + MakePath + ";" + PathWithoutCygwin);
				Environment.SetEnvironmentVariable("PATH", CMakePath + ";" + MakePath + ";" + Environment.GetEnvironmentVariable("PATH"));
				LogInformation("set {0}={1}", "PATH", Environment.GetEnvironmentVariable("PATH"));
			}
		}
	}

	private void SetupInstanceBuildEnvironment()
	{
		// ================================================================================
		// HTML5
		if (GetTargetPlatforms().Any(X => X.Platform == UnrealTargetPlatform.HTML5))
		{
			// override BuildConfiguration defaults - so we can use HTML5SDKInfo
			string EngineSourceDir = GetProjectDirectory(PhysXTargetLib.PhysX, new TargetPlatformData(UnrealTargetPlatform.HTML5)).ToString();
			EngineSourceDir = Regex.Replace(EngineSourceDir, @"\\", "/");
			EngineSourceDir = Regex.Replace(EngineSourceDir, ".*Engine/", "");

			if (!HTML5SDKInfo.IsSDKInstalled())
			{
				throw new AutomationException("EMSCRIPTEN SDK TOOLCHAIN NOT FOUND...");
			}
			// warm up emscripten config file
			HTML5SDKInfo.SetUpEmscriptenConfigFile(true);
			Environment.SetEnvironmentVariable("PATH",
					Environment.GetEnvironmentVariable("EMSCRIPTEN") + ";" +
					Environment.GetEnvironmentVariable("NODEPATH") + ";" +
					Environment.GetEnvironmentVariable("LLVM") + ";" +
					Path.GetDirectoryName(HTML5SDKInfo.Python().FullName) + ";" +
					Environment.GetEnvironmentVariable("PATH"));
			//Log("set {0}={1}", "PATH", Environment.GetEnvironmentVariable("PATH"));
		}
	}

	private static void BuildMSBuildTarget(PhysXTargetLib TargetLib, TargetPlatformData TargetData, List<string> TargetConfigurations, WindowsCompiler TargetWindowsCompiler = WindowsCompiler.VisualStudio2015_DEPRECATED)
	{
		string SolutionFile = GetTargetLibSolutionFileName(TargetLib, TargetData, TargetWindowsCompiler).ToString();
		string MSDevExe = GetMsDevExe(TargetData);

		if (!FileExists(SolutionFile))
		{
			throw new AutomationException(String.Format("Unabled to build Solution {0}. Solution file not found.", SolutionFile));
		}
		if (String.IsNullOrEmpty(MSDevExe))
		{
			throw new AutomationException(String.Format("Unabled to build Solution {0}. devenv.com not found.", SolutionFile));
		}

		foreach (string BuildConfig in TargetConfigurations)
		{
			string CmdLine = String.Format("\"{0}\" /build \"{1}\"", SolutionFile, BuildConfig);
			RunAndLog(BuildCommand.CmdEnv, MSDevExe, CmdLine);
		}
	}

	private static void BuildXboxTarget(PhysXTargetLib TargetLib, TargetPlatformData TargetData, List<string> TargetConfigurations, WindowsCompiler TargetWindowsCompiler = WindowsCompiler.VisualStudio2015_DEPRECATED)
	{
		if (TargetData.Platform != UnrealTargetPlatform.XboxOne)
		{
			return;
		}

		string SolutionFile = GetTargetLibSolutionFileName(TargetLib, TargetData, TargetWindowsCompiler).ToString();
		string MSBuildExe = GetMsBuildExe(TargetData);

		if (!FileExists(SolutionFile))
		{
			throw new AutomationException(String.Format("Unabled to build Solution {0}. Solution file not found.", SolutionFile));
		}
		if (String.IsNullOrEmpty(MSBuildExe))
		{
			throw new AutomationException(String.Format("Unabled to build Solution {0}. msbuild.exe not found.", SolutionFile));
		}

		string AdditionalProperties = "";
		string AutoSDKPropsPath = Environment.GetEnvironmentVariable("XboxOneAutoSDKProp");
		if (AutoSDKPropsPath != null && AutoSDKPropsPath.Length > 0)
		{
			AdditionalProperties += String.Format(";CustomBeforeMicrosoftCommonProps={0}", AutoSDKPropsPath);
		}
		string XboxCMakeModulesPath = Path.Combine(PhysXSourceRootDirectory.FullName, "Externals", "CMakeModules", "XboxOne", "Microsoft.Cpp.Durango.user.props");
		if (File.Exists(XboxCMakeModulesPath))
		{
			AdditionalProperties += String.Format(";ForceImportBeforeCppTargets={0}", XboxCMakeModulesPath);
		}

		foreach (string BuildConfig in TargetConfigurations)
		{
			string CmdLine = String.Format("\"{0}\" /t:build /p:Configuration={1};Platform=Durango{2}", SolutionFile, BuildConfig, AdditionalProperties);
			RunAndLog(BuildCommand.CmdEnv, MSBuildExe, CmdLine);
		}
	}

    private static void BuildSwitchTarget(PhysXTargetLib TargetLib, TargetPlatformData TargetData, List<string> TargetConfigurations, WindowsCompiler TargetWindowsCompiler = WindowsCompiler.VisualStudio2015_DEPRECATED)
    {
        if (TargetData.Platform != UnrealTargetPlatform.Switch)
        {
            return;
        }

        string SolutionFile = GetTargetLibSolutionFileName(TargetLib, TargetData, TargetWindowsCompiler).ToString();
        string MSBuildExe = GetMsBuildExe(TargetData);

        if (!FileExists(SolutionFile))
        {
            throw new AutomationException(String.Format("Unabled to build Solution {0}. Solution file not found.", SolutionFile));
        }
        if (String.IsNullOrEmpty(MSBuildExe))
        {
            throw new AutomationException(String.Format("Unabled to build Solution {0}. msbuild.exe not found.", SolutionFile));
        }

        string AdditionalProperties = "";

        string AutoSDKPropsPath = Environment.GetEnvironmentVariable("SwitchAutoSDKProp");
        if (AutoSDKPropsPath != null && AutoSDKPropsPath.Length > 0)
        {
            AdditionalProperties += String.Format(";CustomBeforeMicrosoftCommonProps={0}", AutoSDKPropsPath);
        }

        string SwitchCMakeModulesPath = Path.Combine(PhysXSourceRootDirectory.FullName, "Externals", "CMakeModules", "Switch", "Microsoft.Cpp.NX-NXFP2-a64.user.props");
        if (File.Exists(SwitchCMakeModulesPath))
        {
            AdditionalProperties += String.Format(";ForceImportBeforeCppTargets={0}", SwitchCMakeModulesPath);
        }

        foreach (string BuildConfig in TargetConfigurations)
        {
            string CmdLine = String.Format("\"{0}\" /t:build /p:Configuration={1};Platform=NX-NXFP2-a64{2}", SolutionFile, BuildConfig, AdditionalProperties);
            RunAndLog(BuildCommand.CmdEnv, MSBuildExe, CmdLine);
        }
    }

    private static void BuildMakefileTarget(PhysXTargetLib TargetLib, TargetPlatformData TargetData, List<string> TargetConfigurations)
	{
		// FIXME: use absolute path
		string MakeCommand = "make";

		// FIXME: "j -16" should be tweakable
		//string MakeOptions = "-j 1";
		string MakeOptions = "-j 16";

		// Bundled GNU make does not pass job number to subprocesses on Windows, work around that...
		if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
		{
			// Redefining the MAKE variable will cause the -j flag to be passed to child make instances.
			MakeOptions = string.Format("{1} \"MAKE={0} {1}\"", MakeCommand, MakeOptions);
		}

		// this will be replaced for HTML5 - see SetupBuildForTargetLibAndPlatform() for details
		Dictionary<string, string> BuildMap = new Dictionary<string, string>()
		{
			{"debug", "debug"},
			{"checked", "checked"},
			{"profile", "profile"},
			{"release", "release"}
		};

		if (TargetData.Platform == UnrealTargetPlatform.Android)
		{
			// Use make from Android toolchain
			string NDKDirectory = Environment.GetEnvironmentVariable("NDKROOT");
	
			// don't register if we don't have an NDKROOT specified
			if (String.IsNullOrEmpty(NDKDirectory))
			{
				throw new AutomationException("NDKROOT is not specified; cannot build Android.");
			}
	
			NDKDirectory = NDKDirectory.Replace("\"", "");
	
			MakeCommand = NDKDirectory + "\\prebuilt\\windows-x86_64\\bin\\make.exe";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.HTML5)
		{
			// Use emscripten toolchain
			MakeCommand = "python";
			MakeOptions = "\"" + HTML5SDKInfo.EMSCRIPTEN_ROOT + "\\emmake\" make";
			BuildMap = new Dictionary<string, string>()
			{
				{"debug", "Build-O0"},
				{"checked", "Build-O2"},
				{"profile", "Build-Oz"},
				{"release", "Build-O3"}
			};
		}

		// makefile build has "projects" for every configuration. However, we abstract away from that by assuming GetProjectDirectory points to the "meta-project"
		foreach (string BuildConfig in TargetConfigurations)
		{
			DirectoryReference MetaProjectDirectory = GetProjectDirectory(TargetLib, TargetData);
			DirectoryReference ConfigDirectory = DirectoryReference.Combine(MetaProjectDirectory, BuildMap[BuildConfig]);
			Environment.SetEnvironmentVariable("LIB_SUFFIX", GetConfigurationSuffix(BuildConfig, TargetData)); // only used in HTML5's CMakefiles
			string Makefile = FileReference.Combine(ConfigDirectory, "Makefile").ToString();
			if (!FileExists(Makefile))
			{
				throw new AutomationException(String.Format("Unabled to build {0} - file not found.", Makefile));
			}

			ProcessStartInfo StartInfo = new ProcessStartInfo();
			StartInfo.FileName = MakeCommand;
			StartInfo.WorkingDirectory = ConfigDirectory.ToString();
			StartInfo.Arguments = MakeOptions;

			LogInformation("Working in: {0}", StartInfo.WorkingDirectory);
			LogInformation("{0} {1}", StartInfo.FileName, StartInfo.Arguments);

			if (RunLocalProcessAndLogOutput(StartInfo) != 0)
			{
				throw new AutomationException(String.Format("Unabled to build {0}. Build process failed.", Makefile));
			}
		}
	}

	private static void BuildXcodeTarget(PhysXTargetLib TargetLib, TargetPlatformData TargetData, List<string> TargetConfigurations)
	{
		DirectoryReference Directory = GetProjectDirectory(TargetLib, TargetData);
        string ProjectName = "";

        switch(TargetLib)
        {
            case PhysXTargetLib.APEX:
                ProjectName = "APEX";
                break;
            case PhysXTargetLib.NvCloth:
                ProjectName = "NvCloth";
                break;
            case PhysXTargetLib.PhysX:
                ProjectName = "PhysX";
                break;
            default:
                throw new AutomationException(String.Format("Unabled to build XCode target, Unsupported library {0}.", TargetLib.ToString()));
        }

		string ProjectFile = FileReference.Combine(Directory, ProjectName + ".xcodeproj").ToString();

		if (!DirectoryExists(ProjectFile))
		{
			throw new AutomationException(String.Format("Unabled to build project {0}. Project file not found.", ProjectFile));
		}

		foreach (string BuildConfig in TargetConfigurations)
		{
			string CmdLine = String.Format("-project \"{0}\" -target=\"ALL_BUILD\" -configuration {1} -quiet", ProjectFile, BuildConfig);
			RunAndLog(BuildCommand.CmdEnv, "/usr/bin/xcodebuild", CmdLine);
		}
	}

	private static void BuildTargetLibForPlatform(PhysXTargetLib TargetLib, TargetPlatformData TargetData, List<string> TargetConfigurations, List<WindowsCompiler> TargetWindowsCompilers)
	{
		if (DoesPlatformUseMSBuild(TargetData))
		{
			if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 || 
				TargetData.Platform == UnrealTargetPlatform.HoloLens)
			{
				// for windows platforms we support building against multiple compilers
				foreach (WindowsCompiler TargetWindowsCompiler in TargetWindowsCompilers)
				{
					BuildMSBuildTarget(TargetLib, TargetData, TargetConfigurations, TargetWindowsCompiler);
				}
			}
			else if (TargetData.Platform == UnrealTargetPlatform.XboxOne)
			{
				BuildXboxTarget(TargetLib, TargetData, TargetConfigurations);
			}
			else if (TargetData.Platform == UnrealTargetPlatform.Switch)
			{
				BuildSwitchTarget(TargetLib, TargetData, TargetConfigurations);
			}
			else
			{
				BuildMSBuildTarget(TargetLib, TargetData, TargetConfigurations);
			}
		}
		else if (DoesPlatformUseXcode(TargetData))
		{
			BuildXcodeTarget(TargetLib, TargetData, TargetConfigurations);
		}
		else if (DoesPlatformUseMakefiles(TargetData))
		{
			BuildMakefileTarget(TargetLib, TargetData, TargetConfigurations);
		}
		else
		{
			throw new AutomationException(String.Format("Unsupported target platform '{0}' passed to BuildTargetLibForPlatform", TargetData));
		}
	}

	private static DirectoryReference GetPlatformBinaryDirectory(TargetPlatformData TargetData, WindowsCompiler TargetWindowsCompiler)
	{
		string VisualStudioName = string.Empty;
		string ArchName = string.Empty;

		if (DoesPlatformUseMSBuild(TargetData))
		{
			switch (TargetWindowsCompiler)
			{
				case WindowsCompiler.VisualStudio2015_DEPRECATED:
					VisualStudioName = "VS2015";
					break;
				default:
					throw new AutomationException(String.Format("Unsupported visual studio compiler '{0}' supplied to GetOutputBinaryDirectory", TargetWindowsCompiler));
			}
		}

		if (TargetData.Platform == UnrealTargetPlatform.Win32)
		{
			ArchName = "Win32";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Win64)
		{
			ArchName = "Win64";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			ArchName = "HoloLens";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Mac)
		{
			ArchName = "Mac";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			ArchName = "Linux/" + TargetData.Architecture;
		}
		else
		{
			throw new AutomationException(String.Format("Unsupported platform '{0}' supplied to GetOutputBinaryDirectory", TargetData.ToString()));
		}

		return DirectoryReference.Combine(RootOutputBinaryDirectory, ArchName, VisualStudioName);
	}

	private static DirectoryReference GetPlatformLibDirectory(TargetPlatformData TargetData, WindowsCompiler TargetWindowsCompiler)
	{
		string VisualStudioName = string.Empty;
		string ArchName = string.Empty;

		if (DoesPlatformUseMSBuild(TargetData)  &&  TargetData.Platform != UnrealTargetPlatform.Switch)
		{
			switch (TargetWindowsCompiler)
			{
				case WindowsCompiler.VisualStudio2015_DEPRECATED:
					VisualStudioName = "VS2015";
					break;
				default:
					throw new AutomationException(String.Format("Unsupported visual studio compiler '{0}' supplied to GetOutputLibDirectory", TargetWindowsCompiler));
			}
		}

		if (TargetData.Platform == UnrealTargetPlatform.Win32)
		{
			ArchName = "Win32";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Win64)
		{
			ArchName = "Win64";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			ArchName = "HoloLens";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.XboxOne)
		{
			ArchName = "XboxOne";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.PS4)
		{
			ArchName = "PS4";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Switch)
		{
			ArchName = "Switch";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Android)
		{
			switch (TargetData.Architecture)
			{
				default:
				case "arm7": ArchName = "Android/ARMv7"; break;
				case "arm64": ArchName = "Android/ARM64"; break;
				case "x86": ArchName = "Android/x86"; break;
				case "x64": ArchName = "Android/x64"; break;
			}
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			ArchName = "Linux/" + TargetData.Architecture;
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Mac)
		{
			ArchName = "Mac";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.HTML5)
		{
			ArchName = "HTML5";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.IOS)
		{
			ArchName = "IOS";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.TVOS)
		{
			ArchName = "TVOS";
		}
		else
		{
			throw new AutomationException(String.Format("Unsupported platform '{0}' supplied to GetOutputLibDirectory", TargetData.ToString()));
		}

		return DirectoryReference.Combine(RootOutputLibDirectory, ArchName, VisualStudioName);
	}

	private static bool PlatformHasBinaries(TargetPlatformData TargetData)
	{
		return TargetData.Platform == UnrealTargetPlatform.Win32 ||
			TargetData.Platform == UnrealTargetPlatform.Win64 ||
			TargetData.Platform == UnrealTargetPlatform.Mac ||
			TargetData.Platform == UnrealTargetPlatform.Linux ||
			TargetData.Platform == UnrealTargetPlatform.LinuxAArch64 ||
			TargetData.Platform == UnrealTargetPlatform.HoloLens;
	}
	private static bool PlatformUsesDebugDatabase(TargetPlatformData TargetData)
	{
		return TargetData.Platform == UnrealTargetPlatform.Win32 ||
			TargetData.Platform == UnrealTargetPlatform.Win64 ||
			// Target.Platform == UnrealTargetPlatform.Mac || 
			TargetData.Platform == UnrealTargetPlatform.Linux ||
			TargetData.Platform == UnrealTargetPlatform.LinuxAArch64 ||
			TargetData.Platform == UnrealTargetPlatform.XboxOne || 
			TargetData.Platform == UnrealTargetPlatform.HoloLens;
	}
	private static string GetPlatformDebugDatabaseExtension(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 || TargetData.Platform == UnrealTargetPlatform.XboxOne || 
				TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			return "pdb";
		}
		if (TargetData.Platform == UnrealTargetPlatform.Mac)
		{
			return "dSYM";
		}
		if (TargetData.Platform == UnrealTargetPlatform.Mac)
		{
			return "sym";
		}
		throw new AutomationException(String.Format("No debug database extension for platform '{0}'", TargetData.Platform.ToString()));
	}

	private static string GetPlatformBinaryExtension(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 || 
				TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			return "dll";
		}
		if (TargetData.Platform == UnrealTargetPlatform.Mac)
		{
			return "dylib";
		}
		if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			return "so";
		}
		throw new AutomationException(String.Format("No binary extension for platform '{0}'", TargetData.Platform.ToString()));
	}

	private static string GetPlatformLibExtension(TargetPlatformData TargetData)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 || TargetData.Platform == UnrealTargetPlatform.XboxOne || 
				TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			return "lib";
		}
		if (TargetData.Platform == UnrealTargetPlatform.HTML5)
		{
			return "bc";
		}

		// everything else is clang
		return "a";
	}

    private static bool FileGeneratedByLib(string FileNameUpper, PhysXTargetLib TargetLib)
    {
        switch(TargetLib)
        {
            case PhysXTargetLib.APEX:
                return FileGeneratedByAPEX(FileNameUpper);
            case PhysXTargetLib.NvCloth:
                return FileGeneratedByNvCloth(FileNameUpper);
            default:
                break;
        }

        // Must have been PhysX if we got here, if it wasn't generated by other libs, then it's PhysX
        return !FileGeneratedByAPEX(FileNameUpper) && !FileGeneratedByNvCloth(FileNameUpper);
    }

    private static bool FileGeneratedByNvCloth(string FileNameUpper)
    {
		if (FileNameUpper.Contains("NVCLOTH"))
        {
            return true;
        }

        return false;
    }
    
	private static bool FileGeneratedByAPEX(string FileNameUpper)
	{
		if (FileNameUpper.Contains("APEX"))
		{
			return true;
		}
		else
		{
			foreach (string SpecialApexLib in APEXSpecialLibs)
			{
				if (FileNameUpper.Contains(SpecialApexLib.ToUpper()))	//There are some APEX libs that don't use the APEX prefix so make sure to test against it
				{
					return true;
				}
			}
		}

		return false;
	}

	private static void FindOutputFilesHelper(HashSet<FileReference> OutputFiles, DirectoryReference BaseDir, string SearchPrefix, PhysXTargetLib TargetLib)
	{
		if (!DirectoryReference.Exists(BaseDir))
		{
			return;
		}

		foreach (FileReference FoundFile in DirectoryReference.EnumerateFiles(BaseDir, SearchPrefix))
		{
			string FileNameUpper = FoundFile.GetFileName().ToString().ToUpper();
			
			if(FileGeneratedByLib(FileNameUpper, TargetLib))
			{
				OutputFiles.Add(FoundFile);
			}
		}
	}


	private static void GenerateDebugFiles(HashSet<FileReference> OutFiles, PhysXTargetLib TargetLib, TargetPlatformData TargetData, string TargetConfiguration, WindowsCompiler TargetWindowsCompiler = WindowsCompiler.VisualStudio2015_DEPRECATED)
	{
		if (TargetData.Platform == UnrealTargetPlatform.Linux || TargetData.Platform == UnrealTargetPlatform.LinuxAArch64)
		{
			HashSet<FileReference> SoFiles = new HashSet<FileReference>();

			string SearchSuffix = GetConfigurationSuffix(TargetConfiguration, TargetData).ToUpper();
			string SearchPrefix = "*" + SearchSuffix + ".";

			DirectoryReference BinaryDir = GetPlatformBinaryDirectory(TargetData, TargetWindowsCompiler);
			FindOutputFilesHelper(SoFiles, BinaryDir, SearchPrefix + GetPlatformBinaryExtension(TargetData), TargetLib);

			foreach (FileReference SOFile in SoFiles)
			{
				string ExeSuffix = "";
				if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win32 || BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
				{
					ExeSuffix += ".exe";
				}

				FileReference PSymbolFile = FileReference.Combine(SOFile.Directory, SOFile.GetFileNameWithoutExtension() + ".psym");
				FileReference SymbolFile = FileReference.Combine(SOFile.Directory, SOFile.GetFileNameWithoutExtension() + ".sym");

				// dump_syms
				ProcessStartInfo StartInfo = new ProcessStartInfo();
				StartInfo.FileName = DumpSymsPath.ToString() + ExeSuffix;
				StartInfo.Arguments = SOFile.FullName + " " + PSymbolFile.ToString();
				StartInfo.RedirectStandardError = true;

				LogInformation("Running: '{0} {1}'", StartInfo.FileName, StartInfo.Arguments);

				RunLocalProcessAndLogOutput(StartInfo);

				// BreakpadSymbolEncoder
				StartInfo.FileName = BreakpadSymbolEncoderPath.ToString() + ExeSuffix;
				StartInfo.Arguments = PSymbolFile.ToString() + " " + SymbolFile.ToString();

				LogInformation("Running: '{0} {1}'", StartInfo.FileName, StartInfo.Arguments);

				RunLocalProcessAndLogOutput(StartInfo);

				// Clean up the Temp *.psym file, as they are no longer needed
				InternalUtils.SafeDeleteFile(PSymbolFile.ToString());

				OutFiles.Add(SymbolFile);
			}
		}
	}

	private static string GetConfigurationSuffix(string TargetConfiguration, TargetPlatformData TargetData)
	{
		// default
		Dictionary<string, string> BuildSuffix = new Dictionary<string, string>()
		{
			{"debug", "debug"},
			{"checked", "checked"},
			{"profile", "profile"},
			{"release", ""}
		};

		if (TargetData.Platform == UnrealTargetPlatform.HTML5)
		{
			// HTML5 - see SetupBuildForTargetLibAndPlatform() for details
			BuildSuffix = new Dictionary<string, string>()
			{
				{"debug", ""},
				{"checked", "_O2"},
				{"profile", "_Oz"},
				{"release", "_O3"}
			};
		}

		return BuildSuffix[TargetConfiguration];
	}

	private static void FindOutputFiles(HashSet<FileReference> OutputFiles, PhysXTargetLib TargetLib, TargetPlatformData TargetData, string TargetConfiguration, WindowsCompiler TargetWindowsCompiler = WindowsCompiler.VisualStudio2015_DEPRECATED)
	{
		string SearchSuffix = GetConfigurationSuffix(TargetConfiguration, TargetData).ToUpper();
		if (TargetData.Platform == UnrealTargetPlatform.Win32)
		{
			SearchSuffix += "_x86";
		}
		else if (TargetData.Platform == UnrealTargetPlatform.Win64 || 
				TargetData.Platform == UnrealTargetPlatform.HoloLens)
		{
			SearchSuffix += "_x64";
		}
		string SearchPrefix = "*" + SearchSuffix + ".";

		string DebugExtension = PlatformUsesDebugDatabase(TargetData) ? GetPlatformDebugDatabaseExtension(TargetData) : "";

		if (PlatformHasBinaries(TargetData))
		{
			DirectoryReference BinaryDir = GetPlatformBinaryDirectory(TargetData, TargetWindowsCompiler);
			FindOutputFilesHelper(OutputFiles, BinaryDir, SearchPrefix + GetPlatformBinaryExtension(TargetData), TargetLib);

			if (PlatformUsesDebugDatabase(TargetData))
			{
				FindOutputFilesHelper(OutputFiles, BinaryDir, SearchPrefix + DebugExtension, TargetLib);
			}
		}

		DirectoryReference LibDir = GetPlatformLibDirectory(TargetData, TargetWindowsCompiler);
		FindOutputFilesHelper(OutputFiles, LibDir, SearchPrefix + GetPlatformLibExtension(TargetData), TargetLib);

		if (PlatformUsesDebugDatabase(TargetData))
		{
			FindOutputFilesHelper(OutputFiles, LibDir, SearchPrefix + DebugExtension, TargetLib);
		}
	}

	private static bool PlatformSupportsTargetLib(PhysXTargetLib TargetLib, TargetPlatformData TargetData)
	{
		if(TargetLib == PhysXTargetLib.APEX)
		{
			if (TargetData.Platform == UnrealTargetPlatform.Win32 ||
				TargetData.Platform == UnrealTargetPlatform.Win64 ||
				TargetData.Platform == UnrealTargetPlatform.PS4 ||
				TargetData.Platform == UnrealTargetPlatform.XboxOne ||
				TargetData.Platform == UnrealTargetPlatform.Mac ||
				TargetData.Platform == UnrealTargetPlatform.Switch || 
				TargetData.Platform == UnrealTargetPlatform.HoloLens)
			{
				return true;
			}
			if (TargetData.Platform == UnrealTargetPlatform.Linux)
			{
				// only x86_64 Linux supports it.
				return TargetData.Architecture.StartsWith("x86_64");
			}

			return false;
		}

        if(TargetLib == PhysXTargetLib.NvCloth)
        {
			if (TargetData.Platform == UnrealTargetPlatform.Win32 ||
				TargetData.Platform == UnrealTargetPlatform.Win64 ||
				TargetData.Platform == UnrealTargetPlatform.PS4 ||
				TargetData.Platform == UnrealTargetPlatform.XboxOne ||
				TargetData.Platform == UnrealTargetPlatform.Mac ||
				TargetData.Platform == UnrealTargetPlatform.Switch || 
				TargetData.Platform == UnrealTargetPlatform.HoloLens)
			{
				return true;
			}
			if (TargetData.Platform == UnrealTargetPlatform.Linux)
			{
				// only x86_64 Linux supports it.
				return TargetData.Architecture.StartsWith("x86_64");
			}

			return false;
		}

		return true;
	}

	public override void ExecuteBuild()
	{
		SetupStaticBuildEnvironment();
		SetupInstanceBuildEnvironment();

		bool bBuildSolutions = true;
		if (ParseParam("SkipBuildSolutions"))
		{
			bBuildSolutions = false;
		}

		bool bBuildLibraries = true;
		if (ParseParam("SkipBuild"))
		{
			bBuildLibraries = false;
		}

		bool bAutoCreateChangelist = true;
		if (ParseParam("SkipCreateChangelist"))
		{
			bAutoCreateChangelist = false;
		}

		bool bAutoSubmit = bAutoCreateChangelist;
		if (ParseParam("SkipSubmit"))
		{
			bAutoSubmit = false;
		}
		
		// if we don't pass anything, we'll just merge by default
		string RobomergeCommand = ParseParamValue("Robomerge", "").ToLower();
		if(!string.IsNullOrEmpty(RobomergeCommand))
		{
			// for merge default action, add flag to make sure buildmachine commit isn't skipped
			if(RobomergeCommand == "merge")
			{
				RobomergeCommand = "#robomerge[all] #DisregardExcludedAuthors";
			}
			// otherwise add hashtags
			else if(RobomergeCommand == "ignore")
			{
				RobomergeCommand = "#robomerge #ignore";
			}
			else if(RobomergeCommand == "null")
			{
				RobomergeCommand = "#robomerge #null";
			}
			// otherwise the submit will likely fail.
			else
			{
				throw new AutomationException("Invalid Robomerge param passed in {0}.  Must be \"merge\", \"null\", or \"ignore\"", RobomergeCommand);
			}
		}

		// Parse out the libs we want to build
		List<PhysXTargetLib> TargetLibs = GetTargetLibs();

		// get the platforms we want to build for
		List<TargetPlatformData> TargetPlatforms = GetTargetPlatforms();

		// get the platforms we want to build for
		List<WindowsCompiler> TargetWindowsCompilers = GetTargetWindowsCompilers();

		// get the configurations we want to build for
		List<string> TargetConfigurations = GetTargetConfigurations();

		if (bBuildSolutions)
		{
			foreach (PhysXTargetLib TargetLib in TargetLibs)
			{
				// build target lib for all platforms
				foreach (TargetPlatformData TargetData in TargetPlatforms)
				{
					if (!PlatformSupportsTargetLib(TargetLib, TargetData))
					{
						continue;
					}

					SetupBuildForTargetLibAndPlatform(TargetLib, TargetData, TargetConfigurations, TargetWindowsCompilers, false);
				}
			}
		}

		HashSet<FileReference> FilesToReconcile = new HashSet<FileReference>();
		if (bBuildLibraries)
		{
			foreach (PhysXTargetLib TargetLib in TargetLibs)
			{
				// build target lib for all platforms
				foreach (TargetPlatformData TargetData in TargetPlatforms)
				{
					if (!PlatformSupportsTargetLib(TargetLib, TargetData))
					{
						continue;
					}

					HashSet<FileReference> FilesToDelete = new HashSet<FileReference>();
					foreach (string TargetConfiguration in TargetConfigurations)
					{
						// Delete output files before building them
						if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 || 
								TargetData.Platform == UnrealTargetPlatform.HoloLens)
						{
							foreach (WindowsCompiler TargetCompiler in TargetWindowsCompilers)
							{
								FindOutputFiles(FilesToDelete, TargetLib, TargetData, TargetConfiguration, TargetCompiler);
							}
						}
						else
						{ 
							FindOutputFiles(FilesToDelete, TargetLib, TargetData, TargetConfiguration);
						}
					}
					foreach (FileReference FileToDelete in FilesToDelete)
					{
						FilesToReconcile.Add(FileToDelete);
						InternalUtils.SafeDeleteFile(FileToDelete.ToString());
					}

					BuildTargetLibForPlatform(TargetLib, TargetData, TargetConfigurations, TargetWindowsCompilers);

					foreach (string TargetConfiguration in TargetConfigurations)
					{
						GenerateDebugFiles(FilesToReconcile, TargetLib, TargetData, TargetConfiguration);
					}
				}
			}
		}

		int P4ChangeList = InvalidChangeList;
		if (bAutoCreateChangelist)
		{
			string LibDeploymentDesc = "";

            foreach(PhysXTargetLib Lib in TargetLibs)
			{
                if(LibDeploymentDesc.Length != 0)
                {
                    LibDeploymentDesc += " & ";
			}

                LibDeploymentDesc += Lib.ToString();
			}

			foreach (TargetPlatformData TargetData in TargetPlatforms)
			{
				LibDeploymentDesc += " " + TargetData.ToString();
			}

			string RobomergeLine = string.Empty;
			if(!string.IsNullOrEmpty(RobomergeCommand))
			{
				RobomergeLine = Environment.NewLine + RobomergeCommand;
			}
            P4ChangeList = P4.CreateChange(P4Env.Client, String.Format("BuildPhysX.Automation: Deploying {0} libs.", LibDeploymentDesc) + Environment.NewLine + "#rb none" + Environment.NewLine + "#lockdown Nick.Penwarden" + Environment.NewLine + "#tests none" + Environment.NewLine + "#jira none" + Environment.NewLine + "#okforgithub ignore" + RobomergeLine);
		}


		if (P4ChangeList != InvalidChangeList)
		{
			foreach (PhysXTargetLib TargetLib in TargetLibs)
			{
				foreach (string TargetConfiguration in TargetConfigurations)
				{
					//Add any new files that p4 is not yet tracking.
					foreach (TargetPlatformData TargetData in TargetPlatforms)
					{
						if (!PlatformSupportsTargetLib(TargetLib, TargetData))
						{
							continue;
						}


						if (TargetData.Platform == UnrealTargetPlatform.Win32 || TargetData.Platform == UnrealTargetPlatform.Win64 || 
								TargetData.Platform == UnrealTargetPlatform.HoloLens)
						{
							foreach (WindowsCompiler TargetCompiler in TargetWindowsCompilers)
							{
								FindOutputFiles(FilesToReconcile, TargetLib, TargetData, TargetConfiguration, TargetCompiler);
							}
						}
						else
						{
							FindOutputFiles(FilesToReconcile, TargetLib, TargetData, TargetConfiguration);
						}
					}
				}
			}

			foreach (FileReference FileToReconcile in FilesToReconcile)
			{
				P4.Reconcile(P4ChangeList, FileToReconcile.ToString());
			}
		}

		if(bAutoSubmit && (P4ChangeList != InvalidChangeList))
		{
			if (!P4.TryDeleteEmptyChange(P4ChangeList))
			{
				LogInformation("Submitting changelist " + P4ChangeList.ToString());
				int SubmittedChangeList = InvalidChangeList;
				P4.Submit(P4ChangeList, out SubmittedChangeList);
			}
			else
			{
				LogInformation("Nothing to submit!");
			}
		}
	}
}
