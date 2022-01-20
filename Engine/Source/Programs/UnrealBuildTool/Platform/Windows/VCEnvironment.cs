// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text.RegularExpressions;
using System.Diagnostics;
using System.IO;
using System.Linq;
using Microsoft.Win32;
using System.Text;
using EpicGames.Core;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	/// <summary>
	/// Stores information about a Visual C++ installation and compile environment
	/// </summary>
	class VCEnvironment
	{
		/// <summary>
		/// The compiler version
		/// </summary>
		public readonly WindowsCompiler Compiler;

		/// <summary>
		/// The compiler directory
		/// </summary>
		public readonly DirectoryReference CompilerDir;

		/// <summary>
		/// The compiler version
		/// </summary>
		public readonly VersionNumber CompilerVersion;

		/// <summary>
		/// The compiler Architecture
		/// </summary>
		public readonly WindowsArchitecture Architecture;

		/// <summary>
		/// The underlying toolchain to use. Using Clang/ICL will piggy-back on a Visual Studio toolchain for the CRT, linker, etc...
		/// </summary>
		public readonly WindowsCompiler ToolChain;

		/// <summary>
		/// Root directory containing the toolchain
		/// </summary>
		public readonly DirectoryReference ToolChainDir;

		/// <summary>
		/// The toolchain version number
		/// </summary>
		public readonly VersionNumber ToolChainVersion;
		
		/// <summary>
		/// Root directory containing the Windows Sdk
		/// </summary>
		public readonly DirectoryReference WindowsSdkDir;

		/// <summary>
		/// Version number of the Windows Sdk
		/// </summary>
		public readonly VersionNumber WindowsSdkVersion;

		/// <summary>
		/// The path to the linker for linking executables
		/// </summary>
		public readonly FileReference CompilerPath;

		/// <summary>
		/// The path to the linker for linking executables
		/// </summary>
		public readonly FileReference LinkerPath;

		/// <summary>
		/// The path to the linker for linking libraries
		/// </summary>
		public readonly FileReference LibraryManagerPath;

		/// <summary>
		/// Path to the resource compiler from the Windows SDK
		/// </summary>
		public readonly FileReference ResourceCompilerPath;

		/// <summary>
		/// Optional directory containing redistributable items (DLLs etc)
		/// </summary>
		public readonly DirectoryReference? RedistDir = null;

		/// <summary>
		/// The default system include paths
		/// </summary>
		public readonly List<DirectoryReference> IncludePaths = new List<DirectoryReference>();

		/// <summary>
		/// The default system library paths
		/// </summary>
		public readonly List<DirectoryReference> LibraryPaths = new List<DirectoryReference>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Params">Main constructor parameters</param>
		public VCEnvironment(VCEnvironmentParameters Params)
		{
			this.Compiler = Params.Compiler;
			this.CompilerDir = Params.CompilerDir;
			this.CompilerVersion = Params.CompilerVersion;
			this.Architecture = Params.Architecture;
			this.ToolChain = Params.ToolChain;
			this.ToolChainDir = Params.ToolChainDir;
			this.ToolChainVersion = Params.ToolChainVersion;
			this.WindowsSdkDir = Params.WindowsSdkDir;
			this.WindowsSdkVersion = Params.WindowsSdkVersion;
			this.RedistDir = Params.RedistDir;

			// Get the standard VC paths
			DirectoryReference VCToolPath = GetVCToolPath(ToolChain, ToolChainDir, Architecture);

            // Compile using 64 bit tools for 64 bit targets, and 32 for 32.
			CompilerPath = GetCompilerToolPath(Params.Platform, Compiler, Architecture, CompilerDir);

			// Regardless of the target, if we're linking on a 64 bit machine, we want to use the 64 bit linker (it's faster than the 32 bit linker and can handle large linking jobs)
			DirectoryReference DefaultLinkerDir = VCToolPath;
			LinkerPath = GetLinkerToolPath(Params.Platform, Compiler, CompilerDir, DefaultLinkerDir);
			LibraryManagerPath = GetLibraryLinkerToolPath(Params.Platform, Compiler, CompilerDir, DefaultLinkerDir);

			// Get the resource compiler path from the Windows SDK
			ResourceCompilerPath = GetResourceCompilerToolPath(Params.Platform, WindowsSdkDir, WindowsSdkVersion);

			// Get all the system include paths
			SetupEnvironment(Params.Platform);
		}

		/// <summary>
		/// Updates environment variables needed for running with this toolchain
		/// </summary>
		public void SetEnvironmentVariables()
		{
			// Add the compiler path and directory as environment variables for the process so they may be used elsewhere.
			Environment.SetEnvironmentVariable("VC_COMPILER_PATH", CompilerPath.FullName, EnvironmentVariableTarget.Process);
			Environment.SetEnvironmentVariable("VC_COMPILER_DIR", CompilerPath.Directory.FullName, EnvironmentVariableTarget.Process);

			AddDirectoryToPath(GetVCToolPath(ToolChain, ToolChainDir, Architecture));
			if (Architecture == WindowsArchitecture.ARM64)
			{
				// Add both toolchain paths to the PATH environment variable. There are some support DLLs which are only added to one of the paths, but which the toolchain in the other directory
				// needs to run (eg. mspdbcore.dll).
				AddDirectoryToPath(GetVCToolPath(ToolChain, ToolChainDir, WindowsArchitecture.x64));
			}

			// Add the Windows SDK directory to the path too, for mt.exe.
			if (WindowsSdkVersion >= new VersionNumber(10))
			{
				AddDirectoryToPath(DirectoryReference.Combine(WindowsSdkDir, "bin", WindowsSdkVersion.ToString(), Architecture.ToString()));
			}
		}

		/// <summary>
		/// Add a directory to the PATH environment variable
		/// </summary>
		/// <param name="ToolPath">The path to add</param>
		static void AddDirectoryToPath(DirectoryReference ToolPath)
		{
            string PathEnvironmentVariable = Environment.GetEnvironmentVariable("PATH") ?? "";
            if (!PathEnvironmentVariable.Split(';').Any(x => String.Compare(x, ToolPath.FullName, true) == 0))
            {
                PathEnvironmentVariable = ToolPath.FullName + ";" + PathEnvironmentVariable;
                Environment.SetEnvironmentVariable("PATH", PathEnvironmentVariable);
            }
		}

		/// <summary>
		/// Gets the path to the tool binaries.
		/// </summary>
		/// <param name="Compiler">The compiler version</param>
		/// <param name="VCToolChainDir">Base directory for the VC toolchain</param>
		/// <param name="Architecture">Target Architecture</param>
		/// <returns>Directory containing the 32-bit toolchain binaries</returns>
		public static DirectoryReference GetVCToolPath(WindowsCompiler Compiler, DirectoryReference VCToolChainDir, WindowsArchitecture Architecture)
		{
			FileReference NativeCompilerPath = FileReference.Combine(VCToolChainDir, "bin", "HostX64", WindowsExports.GetArchitectureSubpath(Architecture), "cl.exe");
			if (FileReference.Exists(NativeCompilerPath))
			{
				return NativeCompilerPath.Directory;
			}

			FileReference CrossCompilerPath = FileReference.Combine(VCToolChainDir, "bin", "HostX86", WindowsExports.GetArchitectureSubpath(Architecture), "cl.exe");
			if (FileReference.Exists(CrossCompilerPath))
			{
				return CrossCompilerPath.Directory;
			}

			throw new BuildException("No required compiler toolchain found in {0}", VCToolChainDir);
		}

		/// <summary>
		/// Gets the path to the compiler.
		/// </summary>
		static FileReference GetCompilerToolPath(UnrealTargetPlatform Platform, WindowsCompiler Compiler, WindowsArchitecture Architecture, DirectoryReference CompilerDir)
		{
			if (Compiler == WindowsCompiler.Clang)
			{
				return FileReference.Combine(CompilerDir, "bin", "clang-cl.exe");
			}
			else if (Compiler == WindowsCompiler.Intel)
			{
				return FileReference.Combine(CompilerDir, "windows", "bin", "icx.exe");
			}
			return FileReference.Combine(GetVCToolPath(Compiler, CompilerDir, Architecture), "cl.exe");
		}

		/// <summary>
		/// Gets the path to the linker.
		/// </summary>
		static FileReference GetLinkerToolPath(UnrealTargetPlatform Platform, WindowsCompiler Compiler, DirectoryReference CompilerDir, DirectoryReference DefaultLinkerDir)
		{
			// Regardless of the target, if we're linking on a 64 bit machine, we want to use the 64 bit linker (it's faster than the 32 bit linker)
			if (Compiler == WindowsCompiler.Clang && WindowsPlatform.bAllowClangLinker)
			{
				return FileReference.Combine(CompilerDir, "bin", "lld-link.exe");
			}
			else if (Compiler == WindowsCompiler.Intel && WindowsPlatform.bAllowIntelLinker)
			{
				return FileReference.Combine(CompilerDir, "windows", "bin", "intel64", "xilink.exe");
			}
			return FileReference.Combine(DefaultLinkerDir, "link.exe");

		}

		/// <summary>
		/// Gets the path to the library linker.
		/// </summary>
		static FileReference GetLibraryLinkerToolPath(UnrealTargetPlatform Platform, WindowsCompiler Compiler, DirectoryReference CompilerDir, DirectoryReference DefaultLinkerDir)
		{
			// Regardless of the target, if we're linking on a 64 bit machine, we want to use the 64 bit linker (it's faster than the 32 bit linker)
			if (Compiler == WindowsCompiler.Clang && WindowsPlatform.bAllowClangLinker)
			{
				// @todo: lld-link is not currently working for building .lib
				//return FileReference.Combine(CompilerDir, "bin", "lld-link.exe");
			}
			else if (Compiler == WindowsCompiler.Intel && WindowsPlatform.bAllowIntelLinker)
			{
				return FileReference.Combine(CompilerDir, "windows", "bin", "intel64", "xilib.exe");
			}
			return FileReference.Combine(DefaultLinkerDir, "lib.exe");
		}

		/// <summary>
		/// Gets the path to the resource compiler's rc.exe for the specified platform.
		/// </summary>
		virtual protected FileReference GetResourceCompilerToolPath(UnrealTargetPlatform Platform, DirectoryReference WindowsSdkDir, VersionNumber WindowsSdkVersion)
		{
			// 64 bit -- we can use the 32 bit version to target 64 bit on 32 bit OS.
			FileReference ResourceCompilerPath = FileReference.Combine(WindowsSdkDir, "bin", WindowsSdkVersion.ToString(), "x64", "rc.exe");
			if(FileReference.Exists(ResourceCompilerPath))
			{
				return ResourceCompilerPath;
			}

			ResourceCompilerPath = FileReference.Combine(WindowsSdkDir, "bin", "x64", "rc.exe");
			if(FileReference.Exists(ResourceCompilerPath))
			{
				return ResourceCompilerPath;
			}

			throw new BuildException("Unable to find path to the Windows resource compiler under {0} (version {1})", WindowsSdkDir, WindowsSdkVersion);
		}

		/// <summary>
		/// Return the standard Visual C++ library path for the given platform in this toolchain
		/// </summary>
		protected virtual DirectoryReference GetToolChainLibsDir(UnrealTargetPlatform Platform)
		{
			string ArchFolder = WindowsExports.GetArchitectureSubpath(Architecture);

			// Add the standard Visual C++ library paths
			if (ToolChain.IsMSVC())
			{
				if (Platform == UnrealTargetPlatform.HoloLens)
				{
					return DirectoryReference.Combine(ToolChainDir, "lib", ArchFolder, "store");
				}
				else
				{
					return DirectoryReference.Combine(ToolChainDir, "lib", ArchFolder);
				}
			}
			else
			{
				DirectoryReference LibsPath = DirectoryReference.Combine(ToolChainDir, "LIB");
				if (Platform == UnrealTargetPlatform.HoloLens)
				{
					LibsPath = DirectoryReference.Combine(LibsPath, "store");
				}

				if (Architecture == WindowsArchitecture.x64)
				{
					LibsPath = DirectoryReference.Combine(LibsPath, "amd64");
				}

				return LibsPath;
			}
		}

		/// <summary>
		/// Sets up the standard compile environment for the toolchain
		/// </summary>
		private void SetupEnvironment(UnrealTargetPlatform Platform)
		{
			string ArchFolder = WindowsExports.GetArchitectureSubpath(Architecture);

			// Add the standard Visual C++ include paths
			IncludePaths.Add(DirectoryReference.Combine(ToolChainDir, "INCLUDE"));

			// Add the standard Visual C++ library paths
			LibraryPaths.Add( GetToolChainLibsDir(Platform));

			// If we're on >= Visual Studio 2015 and using pre-Windows 10 SDK, we need to find a Windows 10 SDK and add the UCRT include paths
			if(ToolChain.IsMSVC() && WindowsSdkVersion < new VersionNumber(10))
			{
				KeyValuePair<VersionNumber, DirectoryReference> Pair = MicrosoftPlatformSDK.FindUniversalCrtDirs().OrderByDescending(x => x.Key).FirstOrDefault();
				if(Pair.Key == null || Pair.Key < new VersionNumber(10))
				{
					throw new BuildException("{0} requires the Universal CRT to be installed.", WindowsPlatform.GetCompilerName(ToolChain));
				}

				DirectoryReference IncludeRootDir = DirectoryReference.Combine(Pair.Value, "include", Pair.Key.ToString());
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "ucrt"));

				DirectoryReference LibraryRootDir = DirectoryReference.Combine(Pair.Value, "lib", Pair.Key.ToString());
				LibraryPaths.Add(DirectoryReference.Combine(LibraryRootDir, "ucrt", ArchFolder));
			}

			// Add the NETFXSDK include path. We need this for SwarmInterface.
			DirectoryReference? NetFxSdkDir;
			if(MicrosoftPlatformSDK.TryGetNetFxSdkInstallDir(out NetFxSdkDir))
			{
				IncludePaths.Add(DirectoryReference.Combine(NetFxSdkDir, "include", "um"));
				LibraryPaths.Add(DirectoryReference.Combine(NetFxSdkDir, "lib", "um", ArchFolder));
			}
			else
			{
				throw new BuildException("Could not find NetFxSDK install dir; this will prevent SwarmInterface from installing.  Install a version of .NET Framework SDK at 4.6.0 or higher.");
			}

			// Add the Windows SDK paths
			if (WindowsSdkVersion >= new VersionNumber(10))
			{
				DirectoryReference IncludeRootDir = DirectoryReference.Combine(WindowsSdkDir, "include", WindowsSdkVersion.ToString());
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "ucrt"));
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "shared"));
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "um"));
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "winrt"));

				DirectoryReference LibraryRootDir = DirectoryReference.Combine(WindowsSdkDir, "lib", WindowsSdkVersion.ToString());
				LibraryPaths.Add(DirectoryReference.Combine(LibraryRootDir, "ucrt", ArchFolder));
				LibraryPaths.Add(DirectoryReference.Combine(LibraryRootDir, "um", ArchFolder));
			}
			else
			{
				DirectoryReference IncludeRootDir = DirectoryReference.Combine(WindowsSdkDir, "include");
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "shared"));
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "um"));
				IncludePaths.Add(DirectoryReference.Combine(IncludeRootDir, "winrt"));

				DirectoryReference LibraryRootDir = DirectoryReference.Combine(WindowsSdkDir, "lib", "winv6.3");
				LibraryPaths.Add(DirectoryReference.Combine(LibraryRootDir, "um", ArchFolder));
			}

			// Add path to Intel math libraries when using Intel oneAPI
			if (Compiler == WindowsCompiler.Intel)
			{
				IncludePaths.Add(DirectoryReference.Combine(CompilerDir, "windows", "compiler", "include"));
				LibraryPaths.Add(DirectoryReference.Combine(CompilerDir, "windows", "compiler", "lib", "intel64"));
			}
		}


		/// <summary>
		/// Creates an environment with the given settings
		/// </summary>
		/// <param name="Compiler">The compiler version to use</param>
		/// <param name="Platform">The platform to target</param>
		/// <param name="Architecture">The Architecture to target</param>
		/// <param name="CompilerVersion">The specific toolchain version to use</param>
		/// <param name="WindowsSdkVersion">Version of the Windows SDK to use</param>
		/// <param name="SuppliedSdkDirectoryForVersion">If specified, this is the SDK directory to use, otherwise, attempt to look up via registry. If specified, the WindowsSdkVersion is used directly</param>
		/// <returns>New environment object with paths for the given settings</returns>
		public static VCEnvironment Create(WindowsCompiler Compiler, UnrealTargetPlatform Platform, WindowsArchitecture Architecture, string? CompilerVersion, string? WindowsSdkVersion, string? SuppliedSdkDirectoryForVersion)
		{
			return Create( new VCEnvironmentParameters(Compiler, Platform, Architecture, CompilerVersion, WindowsSdkVersion, SuppliedSdkDirectoryForVersion) );
		}

		/// <summary>
		/// Creates an environment with the given parameters
		/// </summary>
		public static VCEnvironment Create( VCEnvironmentParameters Params)
		{
			return new VCEnvironment(Params);
		}


	}

	/// <summary>
	/// Parameter structure for constructing VCEnvironment
	/// </summary>
	struct VCEnvironmentParameters
	{
		/// <summary>The platform to find the compiler for</summary>
		public UnrealTargetPlatform Platform;

		/// <summary>The compiler to use</summary>
		public WindowsCompiler Compiler;

		/// <summary>The compiler directory</summary>
		public DirectoryReference CompilerDir;

		/// <summary>The compiler version number</summary>
		public VersionNumber CompilerVersion;

		/// <summary>The compiler Architecture</summary>
		public WindowsArchitecture Architecture;

		/// <summary>The base toolchain version</summary>
		public WindowsCompiler ToolChain;

		/// <summary>Directory containing the toolchain</summary>
		public DirectoryReference ToolChainDir;

		/// <summary>Version of the toolchain</summary>
		public VersionNumber ToolChainVersion;

		/// <summary>Root directory containing the Windows Sdk</summary>
		public DirectoryReference WindowsSdkDir;

		/// <summary>Version of the Windows Sdk</summary>
		public VersionNumber WindowsSdkVersion;

		/// <summary>Optional directory for redistributable items (DLLs etc)</summary>
		public DirectoryReference? RedistDir;	   

		/// <summary>
		/// Creates VC environment construction parameters with the given settings
		/// </summary>
		/// <param name="Compiler">The compiler version to use</param>
		/// <param name="Platform">The platform to target</param>
		/// <param name="Architecture">The Architecture to target</param>
		/// <param name="CompilerVersion">The specific toolchain version to use</param>
		/// <param name="WindowsSdkVersion">Version of the Windows SDK to use</param>
		/// <param name="SuppliedSdkDirectoryForVersion">If specified, this is the SDK directory to use, otherwise, attempt to look up via registry. If specified, the WindowsSdkVersion is used directly</param>
		/// <returns>Creation parameters for VC environment</returns>
		public VCEnvironmentParameters (WindowsCompiler Compiler, UnrealTargetPlatform Platform, WindowsArchitecture Architecture, string? CompilerVersion, string? WindowsSdkVersion, string? SuppliedSdkDirectoryForVersion)
		{
			// Get the compiler version info
			VersionNumber? SelectedCompilerVersion;
			DirectoryReference? SelectedCompilerDir;
			DirectoryReference? SelectedRedistDir;
			if(!WindowsPlatform.TryGetToolChainDir(Compiler, CompilerVersion, Architecture, out SelectedCompilerVersion, out SelectedCompilerDir, out SelectedRedistDir))
			{
				throw new BuildException("{0}{1} {2} must be installed in order to build this target.", WindowsPlatform.GetCompilerName(Compiler), String.IsNullOrEmpty(CompilerVersion)? "" : String.Format(" ({0})", CompilerVersion), Architecture.ToString());
			}

			// Get the toolchain info
			WindowsCompiler ToolChain;
			VersionNumber? SelectedToolChainVersion;
			DirectoryReference? SelectedToolChainDir;
			if(Compiler.IsClang())
			{
				if (WindowsPlatform.TryGetToolChainDir(WindowsCompiler.VisualStudio2019, null, Architecture, out SelectedToolChainVersion, out SelectedToolChainDir, out SelectedRedistDir))
				{
					ToolChain = WindowsCompiler.VisualStudio2019;
				}
				else if (WindowsPlatform.TryGetToolChainDir(WindowsCompiler.VisualStudio2022, null, Architecture, out SelectedToolChainVersion, out SelectedToolChainDir, out SelectedRedistDir))
				{
					ToolChain = WindowsCompiler.VisualStudio2022;
				}
				else
				{
					throw new BuildException("{0} or {1} must be installed in order to build this target.", WindowsPlatform.GetCompilerName(WindowsCompiler.VisualStudio2019), WindowsPlatform.GetCompilerName(WindowsCompiler.VisualStudio2022));
				}
			}
			else
			{
				ToolChain = Compiler;
				SelectedToolChainVersion = SelectedCompilerVersion;
				SelectedToolChainDir = SelectedCompilerDir;
			}

			// Get the actual Windows SDK directory
			VersionNumber? SelectedWindowsSdkVersion;
			DirectoryReference? SelectedWindowsSdkDir;
			if (SuppliedSdkDirectoryForVersion != null)
			{
				SelectedWindowsSdkDir = new DirectoryReference(SuppliedSdkDirectoryForVersion);
				SelectedWindowsSdkVersion = VersionNumber.Parse(WindowsSdkVersion!);

				if (!DirectoryReference.Exists(SelectedWindowsSdkDir))
				{
					throw new BuildException("Windows SDK{0} must be installed at {1}.", String.IsNullOrEmpty(WindowsSdkVersion) ? "" : String.Format(" ({0})", WindowsSdkVersion), SuppliedSdkDirectoryForVersion);
				}
			}
			else
			{
				if (!WindowsPlatform.TryGetWindowsSdkDir(WindowsSdkVersion, out SelectedWindowsSdkVersion, out SelectedWindowsSdkDir))
				{
					throw new BuildException("Windows SDK{0} must be installed in order to build this target.", String.IsNullOrEmpty(WindowsSdkVersion) ? "" : String.Format(" ({0})", WindowsSdkVersion));
				}
			}

			// Store the final parameters
			this.Platform = Platform;
			this.Compiler = Compiler;
			this.CompilerDir = SelectedCompilerDir;
			this.CompilerVersion = SelectedCompilerVersion;
			this.Architecture = Architecture;
			this.ToolChain = ToolChain;
			this.ToolChainDir = SelectedToolChainDir;
			this.ToolChainVersion = SelectedToolChainVersion;
			this.WindowsSdkDir = SelectedWindowsSdkDir;
			this.WindowsSdkVersion = SelectedWindowsSdkVersion;
			this.RedistDir = SelectedRedistDir;
		}
	}
}
