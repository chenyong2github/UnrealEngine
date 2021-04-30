// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Text;
using System.Diagnostics;
using System.IO;
using Microsoft.Win32;
using System.Linq;
using EpicGames.Core;
using Microsoft.VisualStudio.Setup.Configuration;
using System.Runtime.InteropServices;
using System.Diagnostics.CodeAnalysis;
using System.Buffers.Binary;

namespace UnrealBuildTool
{
	/// <summary>
	/// Available compiler toolchains on Windows platform
	/// </summary>
	public enum WindowsCompiler
	{
		/// <summary>
		/// Use the default compiler. A specific value will always be used outside of configuration classes.
		/// </summary>
		Default,

		/// <summary>
		/// Use Clang for Windows, using the clang-cl driver.
		/// </summary>
		Clang,

		/// <summary>
		/// Use the Intel C++ compiler
		/// </summary>
		Intel,

		/// <summary>
		/// Visual Studio 2015 (Visual C++ 14.0)
		/// </summary>
		VisualStudio2015_DEPRECATED,

		/// <summary>
		/// Visual Studio 2015 (Visual C++ 14.0)
		/// </summary>
		[Obsolete("UE4 does not support building Visual Studio 2015 targets from the 4.22 release onwards.")]
		VisualStudio2015 = VisualStudio2015_DEPRECATED,

		/// <summary>
		/// Visual Studio 2017 (Visual C++ 15.0)
		/// </summary>
		VisualStudio2017,

		/// <summary>
		/// Visual Studio 2019 (Visual C++ 16.0)
		/// </summary>
		VisualStudio2019,
	}

	/// <summary>
	/// Which static analyzer to use
	/// </summary>
	public enum WindowsStaticAnalyzer
	{
		/// <summary>
		/// Do not perform static analysis
		/// </summary>
		None,

		/// <summary>
		/// Use the default static analyzer for the selected compiler, if it has one. For
		/// Visual Studio and Clang, this means using their built-in static analysis tools.
		/// Any compiler that doesn't support static analysis will ignore this option.
		/// </summary>
		Default, 

		/// <summary>
		/// Use the built-in Visual C++ static analyzer
		/// </summary>
		VisualCpp = Default,

		/// <summary>
		/// Use PVS-Studio for static analysis
		/// </summary>
		PVSStudio,
	}

	/// <summary>
	/// Available architectures on Windows platform
	/// </summary>
	public enum WindowsArchitecture
	{
		/// <summary>
		/// x64
		/// </summary>
		x64,
		/// <summary>
		/// ARM64
		/// </summary>
		ARM64,
	}

	/// <summary>
	/// Windows-specific target settings
	/// </summary>
	public class WindowsTargetRules
	{
		/// <summary>
		/// The target rules which owns this object. Used to resolve some properties.
		/// </summary>
		TargetRules Target;

		/// <summary>
		/// Version of the compiler toolchain to use on Windows platform. A value of "default" will be changed to a specific version at UBT start up.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "CompilerVersion")]
		[XmlConfigFile(Category = "WindowsPlatform")]
		[CommandLine("-2015", Value = "VisualStudio2015")]
		[CommandLine("-2017", Value = "VisualStudio2017")]
		[CommandLine("-2019", Value = "VisualStudio2019")]
		[CommandLine("-Compiler=")]
		public WindowsCompiler Compiler = WindowsCompiler.Default;

		/// <summary>
		/// Architecture of Target.
		/// </summary>
		public WindowsArchitecture Architecture
		{
			get;
			internal set;
		}
		= WindowsArchitecture.x64;

		/// <summary>
		/// The specific toolchain version to use. This may be a specific version number (for example, "14.13.26128"), the string "Latest" to select the newest available version, or
		/// the string "Preview" to select the newest available preview version. By default, and if it is available, we use the toolchain version indicated by
		/// WindowsPlatform.DefaultToolChainVersion (otherwise, we use the latest version).
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		[CommandLine("-CompilerVersion")]
		public string? CompilerVersion = null;

		/// <summary>
		/// The specific Windows SDK version to use. This may be a specific version number (for example, "8.1", "10.0" or "10.0.10150.0"), or the string "Latest", to select the newest available version.
		/// By default, and if it is available, we use the Windows SDK version indicated by WindowsPlatform.DefaultWindowsSdkVersion (otherwise, we use the latest version).
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public string? WindowsSdkVersion = null;

		/// <summary>
		/// Value for the WINVER macro, defining the minimum supported Windows version.
		/// </summary>
		public int TargetWindowsVersion = 0x601;

		/// <summary>
		/// Enable PIX debugging (automatically disabled in Shipping and Test configs)
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "bEnablePIXProfiling")]
		public bool bPixProfilingEnabled = true;

		/// <summary>
		/// Enable building with the Win10 SDK instead of the older Win8.1 SDK 
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "bUseWindowsSDK10")]
		public bool bUseWindowsSDK10 = false;

		/// <summary>
		/// Enables runtime ray tracing support.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings", "bEnableRayTracing")]
		public bool bEnableRayTracing = false;

		/// <summary>
		/// The name of the company (author, provider) that created the project.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Game, "/Script/EngineSettings.GeneralProjectSettings", "CompanyName")]
		public string? CompanyName;

		/// <summary>
		/// The project's copyright and/or trademark notices.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Game, "/Script/EngineSettings.GeneralProjectSettings", "CopyrightNotice")]
		public string? CopyrightNotice;

		/// <summary>
		/// The product name.
		/// </summary>
		[ConfigFile(ConfigHierarchyType.Game, "/Script/EngineSettings.GeneralProjectSettings", "ProjectName")]
		public string? ProductName;

		/// <summary>
		/// The static analyzer to use.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		[CommandLine("-StaticAnalyzer")]
		public WindowsStaticAnalyzer StaticAnalyzer = WindowsStaticAnalyzer.None;

		/// <summary>
		/// Enables address sanitizer (ASan). Only supported for Visual Studio 2019 16.7.0 and up.
		/// </summary>
		[XmlConfigFile(Category = "BuildConfiguration", Name = "bEnableAddressSanitizer")]
		[CommandLine("-EnableASan")]
		public bool bEnableAddressSanitizer = false;

		/// <summary>
		/// Whether we should export a file containing .obj to source file mappings.
		/// </summary>
		[XmlConfigFile]
		[CommandLine("-ObjSrcMap")]
		public string? ObjSrcMapFile = null;

		/// <summary>
		/// Provides a Module Definition File (.def) to the linker to describe various attributes of a DLL.
		/// Necessary when exporting functions by ordinal values instead of by name.
		/// </summary>
		public string? ModuleDefinitionFile;

		/// <summary>
		/// Specifies the path to a manifest file for the linker to embed. Defaults to the manifest in Engine/Build/Windows/Resources. Can be assigned to null
		/// if the target wants to specify its own manifest.
		/// </summary>
		public string? ManifestFile;

		/// <summary>
		/// Enables strict standard conformance mode (/permissive-) in VS2017+.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		[CommandLine("-Strict")]
		public bool bStrictConformanceMode = false; 

		/// VS2015 updated some of the CRT definitions but not all of the Windows SDK has been updated to match.
		/// Microsoft provides legacy_stdio_definitions library to enable building with VS2015 until they fix everything up.
		public bool bNeedsLegacyStdioDefinitionsLib
		{
			get { return Compiler == WindowsCompiler.VisualStudio2015_DEPRECATED || Compiler == WindowsCompiler.VisualStudio2017 || Compiler == WindowsCompiler.VisualStudio2019 || Compiler == WindowsCompiler.Clang; }
		}

		/// <summary>
		/// The stack size when linking
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings")]
		public int DefaultStackSize = 12000000;

		/// <summary>
		/// The stack size to commit when linking
		/// </summary>
		[RequiresUniqueBuildEnvironment]
		[ConfigFile(ConfigHierarchyType.Engine, "/Script/WindowsTargetPlatform.WindowsTargetSettings")]
		public int DefaultStackSizeCommit;

		/// <summary>
		/// Determines the amount of memory that the compiler allocates to construct precompiled headers (/Zm).
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public int PCHMemoryAllocationFactor = 0;

		/// <summary>
		/// True if we allow using addresses larger than 2GB on 32 bit builds
		/// </summary>
		public bool bBuildLargeAddressAwareBinary = true;

		/// <summary>
		/// Create an image that can be hot patched (/FUNCTIONPADMIN)
		/// </summary>
		public bool bCreateHotPatchableImage
		{
			get { return bCreateHotPatchableImagePrivate ?? Target.bWithLiveCoding; }
			set { bCreateHotPatchableImagePrivate = value; }
		}
		private bool? bCreateHotPatchableImagePrivate;

		/// <summary>
		/// Strip unreferenced symbols (/OPT:REF)
		/// </summary>
		public bool bStripUnreferencedSymbols
		{
			get { return bStripUnreferencedSymbolsPrivate ?? ((Target.Configuration == UnrealTargetConfiguration.Test || Target.Configuration == UnrealTargetConfiguration.Shipping) && !Target.bWithLiveCoding); }
			set { bStripUnreferencedSymbolsPrivate = value; }
		}
		private bool? bStripUnreferencedSymbolsPrivate;
			
		/// <summary>
		/// Merge identical COMDAT sections together (/OPT:ICF)
		/// </summary>
		public bool bMergeIdenticalCOMDATs
		{
			get { return bMergeIdenticalCOMDATsPrivate ?? ((Target.Configuration == UnrealTargetConfiguration.Test || Target.Configuration == UnrealTargetConfiguration.Shipping) && !Target.bWithLiveCoding); }
			set { bMergeIdenticalCOMDATsPrivate = value; }
		}
		private bool? bMergeIdenticalCOMDATsPrivate;

		/// <summary>
		/// Whether to put global symbols in their own sections (/Gw), allowing the linker to discard any that are unused.
		/// </summary>
		public bool bOptimizeGlobalData = true;

		/// <summary>
		/// (Experimental) Appends the -ftime-trace argument to the command line for Clang to output a JSON file containing a timeline for the compile. 
		/// See http://aras-p.info/blog/2019/01/16/time-trace-timeline-flame-chart-profiler-for-Clang/ for more info.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public bool bClangTimeTrace = false;

		/// <summary>
		/// Outputs compile timing information so that it can be analyzed.
		/// </summary>
		[XmlConfigFile(Category = "WindowsPlatform")]
		public bool bCompilerTrace = false;

		/// <summary>
		/// Print out files that are included by each source file
		/// </summary>
		[CommandLine("-ShowIncludes")]
		[XmlConfigFile(Category = "WindowsPlatform")]
		public bool bShowIncludes = false;

		/// <summary>
		/// Bundle a working version of dbghelp.dll with the application, and use this to generate minidumps. This works around a bug with the Windows 10 Fall Creators Update (1709)
		/// where rich PE headers larger than a certain size would result in corrupt minidumps.
		/// </summary>
		public bool bUseBundledDbgHelp = true;

		/// <summary>
		/// Settings for PVS studio
		/// </summary>
		public PVSTargetSettings PVS = new PVSTargetSettings();

		/// <summary>
		/// The Visual C++ environment to use for this target. Only initialized after all the target settings are finalized, in ValidateTarget().
		/// </summary>
		internal VCEnvironment? Environment;

		/// <summary>
		/// Directory containing the toolchain
		/// </summary>
		public string? ToolChainDir
		{
			get { return (Environment == null)? null : Environment.ToolChainDir.FullName; }
		}

		/// <summary>
		/// The version number of the toolchain
		/// </summary>
		public string? ToolChainVersion
		{
			get { return (Environment == null)? null : Environment.ToolChainVersion.ToString(); }
		}

		/// <summary>
		/// Root directory containing the Windows Sdk
		/// </summary>
		public string? WindowsSdkDir
		{
			get { return (Environment == null)? null : Environment.WindowsSdkDir.FullName; }
		}

		/// <summary>
		/// Directory containing the DIA SDK
		/// </summary>
		public string DiaSdkDir
		{
			get { return WindowsPlatform.FindDiaSdkDirs(Environment!.Compiler).Select(x => x.FullName).FirstOrDefault(); }
		}

		/// <summary>
		/// Directory containing the IDE package (Professional, Community, etc...)
		/// </summary>
		public string IDEDir
		{
			get
			{
				try
				{
					return WindowsPlatform.FindVisualStudioInstallations(Environment!.Compiler).Select(x => x.BaseDir.FullName).FirstOrDefault();
				}
				catch(Exception) // Find function will throw if there is no visual studio installed! This can happen w/ clang builds
				{
					return string.Empty;
				}
			}
		}

		/// <summary>
		/// When using a Visual Studio compiler, returns the version name as a string
		/// </summary>
		/// <returns>The Visual Studio compiler version name (e.g. "2015")</returns>
		public string GetVisualStudioCompilerVersionName()
		{
			switch (Compiler)
			{
				case WindowsCompiler.Clang:
				case WindowsCompiler.Intel:
				case WindowsCompiler.VisualStudio2015_DEPRECATED:
				case WindowsCompiler.VisualStudio2017:
				case WindowsCompiler.VisualStudio2019:
					return "2015"; // VS2017 is backwards compatible with VS2015 compiler

				default:
					throw new BuildException("Unexpected WindowsCompiler version for GetVisualStudioCompilerVersionName().  Either not using a Visual Studio compiler or switch block needs to be updated");
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Target">The target rules which owns this object</param>
		internal WindowsTargetRules(TargetRules Target)
		{
			this.Target = Target;

			ManifestFile = FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build", "Windows", "Resources", String.Format("Default-{0}.manifest", Target.Platform)).FullName;
		}
	}

	/// <summary>
	/// Read-only wrapper for Windows-specific target settings
	/// </summary>
	public class ReadOnlyWindowsTargetRules
	{
		/// <summary>
		/// The private mutable settings object
		/// </summary>
		private WindowsTargetRules Inner;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Inner">The settings object to wrap</param>
		public ReadOnlyWindowsTargetRules(WindowsTargetRules Inner)
		{
			this.Inner = Inner;
			this.PVS = new ReadOnlyPVSTargetSettings(Inner.PVS);
		}

		/// <summary>
		/// Accessors for fields on the inner TargetRules instance
		/// </summary>
		#region Read-only accessor properties 
		#pragma warning disable CS1591

		public WindowsCompiler Compiler
		{
			get { return Inner.Compiler; }
		}
		
		public WindowsArchitecture Architecture
		{
			get { return Inner.Architecture; }
		}

		public string? CompilerVersion
		{
			get { return Inner.CompilerVersion; }
		}

		public string? WindowsSdkVersion
		{
			get { return Inner.WindowsSdkVersion; }
		}

		public int TargetWindowsVersion
		{
			get { return Inner.TargetWindowsVersion; }
		}

		public bool bPixProfilingEnabled
		{
			get { return Inner.bPixProfilingEnabled; }
		}

		public bool bUseWindowsSDK10
		{
			get { return Inner.bUseWindowsSDK10; }
		}

		public bool bEnableRayTracing
		{
			get { return Inner.bEnableRayTracing; }
		}

		public string? CompanyName
		{
			get { return Inner.CompanyName; }
		}

		public string? CopyrightNotice
		{
			get { return Inner.CopyrightNotice; }
		}

		public string? ProductName
		{
			get { return Inner.ProductName; }
		}

		public WindowsStaticAnalyzer StaticAnalyzer
		{
			get { return Inner.StaticAnalyzer; }
		}

		public bool bEnableAddressSanitizer
		{ 
			get { return Inner.bEnableAddressSanitizer; }
		}

		public string? ObjSrcMapFile
		{
			get { return Inner.ObjSrcMapFile; }
		}

		public string? ModuleDefinitionFile
		{
			get { return Inner.ModuleDefinitionFile; }
		}

		public string? ManifestFile
		{
			get { return Inner.ManifestFile; }
		}

		public bool bNeedsLegacyStdioDefinitionsLib
		{
			get { return Inner.bNeedsLegacyStdioDefinitionsLib; }
		}

		public bool bStrictConformanceMode
		{
			get { return Inner.bStrictConformanceMode; }
		}

		public int DefaultStackSize
		{
			get { return Inner.DefaultStackSize; }
		}

		public int DefaultStackSizeCommit
		{
			get { return Inner.DefaultStackSizeCommit; }
		}

		public int PCHMemoryAllocationFactor
		{
			get { return Inner.PCHMemoryAllocationFactor; }
		}

		public bool bBuildLargeAddressAwareBinary
		{
			get { return Inner.bBuildLargeAddressAwareBinary; }
		}

		public bool bCreateHotpatchableImage
		{
			get { return Inner.bCreateHotPatchableImage; }
		}

		public bool bStripUnreferencedSymbols
		{
			get { return Inner.bStripUnreferencedSymbols; }
		}

		public bool bMergeIdenticalCOMDATs
		{
			get { return Inner.bMergeIdenticalCOMDATs; }
		}

		public bool bOptimizeGlobalData
		{
			get { return Inner.bOptimizeGlobalData; }
		}

		public bool bClangTimeTrace
		{
			get { return Inner.bClangTimeTrace; }
		}

		public bool bCompilerTrace
		{
			get { return Inner.bCompilerTrace; }
		}

		public bool bShowIncludes
		{
			get { return Inner.bShowIncludes; }
		}

		public string GetVisualStudioCompilerVersionName()
		{
			return Inner.GetVisualStudioCompilerVersionName();
		}

		public bool bUseBundledDbgHelp
		{
			get { return Inner.bUseBundledDbgHelp; }
		}

		public ReadOnlyPVSTargetSettings PVS
		{
			get; private set;
		}

		internal VCEnvironment? Environment
		{
			get { return Inner.Environment; }
		}

		public string? ToolChainDir
		{
			get { return Inner.ToolChainDir; }
		}

		public string? ToolChainVersion
		{
			get { return Inner.ToolChainVersion; }
		}

		public string? WindowsSdkDir
		{
			get { return Inner.WindowsSdkDir; }
		}

		public string DiaSdkDir
		{
			get { return Inner.DiaSdkDir; }
		}
		
		public string IDEDir
		{
			get { return Inner.IDEDir; }
		}

		public string GetArchitectureSubpath()
		{
			return WindowsExports.GetArchitectureSubpath(Architecture);
		}

		#pragma warning restore CS1591
		#endregion
	}

	class WindowsPlatform : UEBuildPlatform
	{
		/// <summary>
		/// Information about a particular Visual Studio installation
		/// </summary>
		[DebuggerDisplay("{BaseDir}")]
		public class VisualStudioInstallation
		{
			/// <summary>
			/// Base directory for the installation
			/// </summary>
			public DirectoryReference BaseDir;

			/// <summary>
			/// Whether it's an express edition of Visual Studio. These versions do not contain a 64-bit compiler.
			/// </summary>
			public bool bExpress;

			/// <summary>
			/// Whether it's a pre-release version of the IDE.
			/// </summary>
			public bool bPreview;

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="BaseDir">Base directory for the installation</param>
			public VisualStudioInstallation(DirectoryReference BaseDir)
			{
				this.BaseDir = BaseDir;
			}
		}

		/// <summary>
		/// Information about a particular toolchain installation
		/// </summary>
		class ToolChainInstallation
		{
			/// <summary>
			/// The version "family" (ie. the nominal version number of the directory this toolchain is installed to)
			/// </summary>
			public VersionNumber Family { get; }

			/// <summary>
			/// Index into the preferred version range
			/// </summary>
			public int FamilyRank { get; }

			/// <summary>
			/// The actual version number of this toolchain
			/// </summary>
			public VersionNumber Version { get; }

			/// <summary>
			/// Whether this is a 64-bit toolchain
			/// </summary>
			public bool Is64Bit { get; }

			/// <summary>
			/// Whether it's a pre-release version of the toolchain.
			/// </summary>
			public bool IsPreview { get; }

			/// <summary>
			/// Reason for this toolchain not being compatible
			/// </summary>
			public string? Error { get; }

			/// <summary>
			/// Base directory for the toolchain
			/// </summary>
			public DirectoryReference BaseDir { get; }

			/// <summary>
			/// Constructor
			/// </summary>
			/// <param name="Family"></param>
			/// <param name="FamilyRank"></param>
			/// <param name="Version"></param>
			/// <param name="Is64Bit"></param>
			/// <param name="IsPreview">Whether it's a pre-release version of the toolchain</param>
			/// <param name="Error"></param>
			/// <param name="BaseDir">Base directory for the toolchain</param>
			public ToolChainInstallation(VersionNumber Family, int FamilyRank, VersionNumber Version, bool Is64Bit, bool IsPreview, string? Error, DirectoryReference BaseDir)
			{
				this.Family = Family;
				this.FamilyRank = FamilyRank;
				this.Version = Version;
				this.Is64Bit = Is64Bit;
				this.IsPreview = IsPreview;
				this.Error = Error;
				this.BaseDir = BaseDir;
			}
		}

		/// <summary>
		/// The default compiler version to be used, if installed. 
		/// </summary>
		static readonly VersionNumberRange[] PreferredClangVersions =
		{
			VersionNumberRange.Parse("10.0.0", "10.999")
		};

		/// <summary>
		/// Ranges of tested compiler toolchains to be used, in order of preference. If multiple toolchains in a range are present, the latest version will be preferred.
		/// Note that the numbers here correspond to the installation *folders* rather than precise executable versions. 
		/// </summary>
		static readonly VersionNumberRange[] PreferredVisualCppVersions = new VersionNumberRange[]
		{
			VersionNumberRange.Parse("14.24.27905", "14.28.29913") // VS2019 16.2.3 -> 19.1
		};

		/// <summary>
		/// Cache of Visual Studio installation directories
		/// </summary>
		private static Dictionary<WindowsCompiler, List<VisualStudioInstallation>> CachedVisualStudioInstallations = new Dictionary<WindowsCompiler, List<VisualStudioInstallation>>();

		/// <summary>
		/// Cache of Visual C++ installation directories
		/// </summary>
		private static Dictionary<WindowsCompiler, List<ToolChainInstallation>> CachedToolChainInstallations = new Dictionary<WindowsCompiler, List<ToolChainInstallation>>();

		/// <summary>
		/// Cache of DIA SDK installation directories
		/// </summary>
		private static Dictionary<WindowsCompiler, List<DirectoryReference>> CachedDiaSdkDirs = new Dictionary<WindowsCompiler, List<DirectoryReference>>();

		/// <summary>
		/// True if we should use the Clang linker (LLD) when we are compiling with Clang, otherwise we use the MSVC linker
		/// </summary>
		public static readonly bool bAllowClangLinker = false;

		/// <summary>
		/// True if we should use the Intel linker (xilink) when we are compiling with ICL, otherwise we use the MSVC linker
		/// </summary>
		public static readonly bool bAllowICLLinker = true;

		/// <summary>
		/// True if we allow using addresses larger than 2GB on 32 bit builds
		/// </summary>
		public static readonly bool bBuildLargeAddressAwareBinary = true;

		MicrosoftPlatformSDK SDK;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InPlatform">Creates a windows platform with the given enum value</param>
		/// <param name="InSDK">The installed Windows SDK</param>
		public WindowsPlatform(UnrealTargetPlatform InPlatform, MicrosoftPlatformSDK InSDK)
			: base(InPlatform, InSDK)
		{
			SDK = InSDK;
		}

		/// <summary>
		/// Reset a target's settings to the default
		/// </summary>
		/// <param name="Target"></param>
		public override void ResetTarget(TargetRules Target)
		{
			base.ResetTarget(Target);
		}

		/// <summary>
		/// Creates the VCEnvironment object used to control compiling and other tools. Virtual to allow other platforms to override behavior
		/// </summary>
		/// <param name="Target">Stanard target object</param>
		/// <returns></returns>
		protected virtual VCEnvironment CreateVCEnvironment(TargetRules Target)
		{
			return VCEnvironment.Create(Target.WindowsPlatform.Compiler, Platform, Target.WindowsPlatform.Architecture, Target.WindowsPlatform.CompilerVersion, Target.WindowsPlatform.WindowsSdkVersion, null);
		}

		/// <summary>
		/// Validate a target's settings
		/// </summary>
		public override void ValidateTarget(TargetRules Target)
		{
			if (Platform == UnrealTargetPlatform.HoloLens && Target.Architecture.ToLower() == "arm64")
			{
				Target.WindowsPlatform.Architecture = WindowsArchitecture.ARM64;
				Log.TraceInformation("Using Windows ARM64 architecture");
			}
			else if (Platform == UnrealTargetPlatform.Win64)
			{
				Target.WindowsPlatform.Architecture = WindowsArchitecture.x64;
			}

			// Disable Simplygon support if compiling against the NULL RHI.
			if (Target.GlobalDefinitions.Contains("USE_NULL_RHI=1"))
			{				
				Target.bCompileCEF3 = false;
			}

			// Disable static analysis if not appropriate for the compiler.
			if (Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.Default)
			{
				if (Target.WindowsPlatform.Compiler == WindowsCompiler.Intel)
				{
					Target.WindowsPlatform.StaticAnalyzer = WindowsStaticAnalyzer.None;
				}
			}

			// Set the compiler version if necessary
			if (Target.WindowsPlatform.Compiler == WindowsCompiler.Default)
			{
				if (Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.PVSStudio && HasCompiler(WindowsCompiler.VisualStudio2019))
				{
					Target.WindowsPlatform.Compiler = WindowsCompiler.VisualStudio2019;
				}
				else
				{
					Target.WindowsPlatform.Compiler = GetDefaultCompiler(Target.ProjectFile);
				}
			}

			// Disable linking if we're using a static analyzer
			if(Target.WindowsPlatform.StaticAnalyzer != WindowsStaticAnalyzer.None)
			{
				Target.bDisableLinking = true;
			}

			 // Disable PCHs for PVS studio and clang static analyzer.
			if(Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.PVSStudio)
			{
				Target.bUsePCHFiles = false;
			}
			else if (Target.WindowsPlatform.Compiler == WindowsCompiler.Clang && Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.Default)
			{
				Target.bUsePCHFiles = false;
			}

			// Override PCH settings
			if (Target.WindowsPlatform.Compiler == WindowsCompiler.Intel)
			{
				Target.NumIncludedBytesPerUnityCPP = Math.Min(Target.NumIncludedBytesPerUnityCPP, 256 * 1024);

				Target.bUseSharedPCHs = false;

				Target.bUsePCHFiles = false;
			}

			// E&C support.
			if (Target.bSupportEditAndContinue || Target.bAdaptiveUnityEnablesEditAndContinue)
			{
				Target.bUseIncrementalLinking = true;
			}
			if (Target.bAdaptiveUnityEnablesEditAndContinue && !Target.bAdaptiveUnityDisablesPCH && !Target.bAdaptiveUnityCreatesDedicatedPCH)
			{
				throw new BuildException("bAdaptiveUnityEnablesEditAndContinue requires bAdaptiveUnityDisablesPCH or bAdaptiveUnityCreatesDedicatedPCH");
			}

			// If we're using PDB files and PCHs, the generated code needs to be compiled with the same options as the PCH.
			if ((Target.bUsePDBFiles || Target.bSupportEditAndContinue) && Target.bUsePCHFiles)
			{
				Target.bDisableDebugInfoForGeneratedCode = false;
			}

			Target.bCompileISPC = true;

			// Initialize the VC environment for the target, and set all the version numbers to the concrete values we chose
			Target.WindowsPlatform.Environment = CreateVCEnvironment(Target);

			// pull some things from it
			Target.WindowsPlatform.Compiler = Target.WindowsPlatform.Environment.Compiler;
			Target.WindowsPlatform.CompilerVersion = Target.WindowsPlatform.Environment.CompilerVersion.ToString();
			Target.WindowsPlatform.WindowsSdkVersion = Target.WindowsPlatform.Environment.WindowsSdkVersion.ToString();

			// If we're enabling support for C++ modules, make sure the compiler supports it. VS 16.8 changed which command line arguments are used to enable modules support.
			if (Target.bEnableCppModules && !ProjectFileGenerator.bGenerateProjectFiles && Target.WindowsPlatform.Environment.CompilerVersion < new VersionNumber(14, 28, 29304))
			{
				throw new BuildException("Support for C++20 modules requires Visual Studio 2019 16.8 preview 3 or later. Compiler Version Targeted: {0}", Target.WindowsPlatform.Environment.CompilerVersion);
			}

			// Ensure we're using recent enough version of Visual Studio to support ASan builds.
			if (Target.WindowsPlatform.bEnableAddressSanitizer && Target.WindowsPlatform.Environment.CompilerVersion < new VersionNumber(14, 27, 0))
			{
				throw new BuildException("Address sanitizer requires Visual Studio 2019 16.7 or later.");
			}

//			@Todo: Still getting reports of frequent OOM issues with this enabled as of 15.7.
//			// Enable fast PDB linking if we're on VS2017 15.7 or later. Previous versions have OOM issues with large projects.
//			if(!Target.bFormalBuild && !Target.bUseFastPDBLinking.HasValue && Target.WindowsPlatform.Compiler >= WindowsCompiler.VisualStudio2017)
//			{
//				VersionNumber Version;
//				DirectoryReference ToolChainDir;
//				if(TryGetVCToolChainDir(Target.WindowsPlatform.Compiler, Target.WindowsPlatform.CompilerVersion, out Version, out ToolChainDir) && Version >= new VersionNumber(14, 14, 26316))
//				{
//					Target.bUseFastPDBLinking = true;
//				}
//			}
		}

		/// <summary>
		/// Gets the default compiler which should be used, if it's not set explicitly by the target, command line, or config file.
		/// </summary>
		/// <returns>The default compiler version</returns>
		internal static WindowsCompiler GetDefaultCompiler(FileReference? ProjectFile)
		{
			// If there's no specific compiler set, try to pick the matching compiler for the selected IDE
			if(ProjectFileGeneratorSettings.Format != null)
			{
				foreach(ProjectFileFormat Format in ProjectFileGeneratorSettings.ParseFormatList(ProjectFileGeneratorSettings.Format))
				{
					if (Format == ProjectFileFormat.VisualStudio2019)
					{
						return WindowsCompiler.VisualStudio2019;
					}
				} 
			}

			// Also check the default format for the Visual Studio project generator
			object? ProjectFormatObject;
			if (XmlConfig.TryGetValue(typeof(VCProjectFileGenerator), "Version", out ProjectFormatObject))
			{
				VCProjectFileFormat ProjectFormat = (VCProjectFileFormat)ProjectFormatObject;
				if (ProjectFormat == VCProjectFileFormat.VisualStudio2019)
				{
					return WindowsCompiler.VisualStudio2019;
				}
			}

			// Check the editor settings too
			ProjectFileFormat PreferredAccessor;
			if(ProjectFileGenerator.GetPreferredSourceCodeAccessor(ProjectFile, out PreferredAccessor))
			{
				if(PreferredAccessor == ProjectFileFormat.VisualStudio2019)
			    {
				    return WindowsCompiler.VisualStudio2019;
			    }
			}

			// Second, default based on what's installed, test for 2015 first
			if (HasCompiler(WindowsCompiler.VisualStudio2019))
			{
				return WindowsCompiler.VisualStudio2019;
			}

			// If we do have a Visual Studio installation, but we're missing just the C++ parts, warn about that.
			DirectoryReference? VSInstallDir;
			if (TryGetVSInstallDir(WindowsCompiler.VisualStudio2019, out VSInstallDir))
			{
				Log.TraceWarning("Visual Studio 2019 is installed, but is missing the C++ toolchain. Please verify that the \"VC++ 2019 toolset\" component is selected in the Visual Studio 2019 installation options.");
			}
			else
			{
				Log.TraceWarning("No Visual C++ installation was found. Please download and install Visual Studio 2019 with C++ components.");
			}

			// Finally, default to VS2019 anyway
			return WindowsCompiler.VisualStudio2019;
		}

		/// <summary>
		/// Returns the human-readable name of the given compiler
		/// </summary>
		/// <param name="Compiler">The compiler value</param>
		/// <returns>Name of the compiler</returns>
		public static string GetCompilerName(WindowsCompiler Compiler)
		{
			switch (Compiler)
			{
				case WindowsCompiler.VisualStudio2015_DEPRECATED:
					return "Visual Studio 2015";
				case WindowsCompiler.VisualStudio2017:
					return "Visual Studio 2017";
				case WindowsCompiler.VisualStudio2019:
					return "Visual Studio 2019";
				default:
					return Compiler.ToString();
			}
		}

		/// <summary>
		/// Get the first Visual Studio install directory for the given compiler version. Note that it is possible for the compiler toolchain to be installed without
		/// Visual Studio.
		/// </summary>
		/// <param name="Compiler">Version of the toolchain to look for.</param>
		/// <param name="InstallDir">On success, the directory that Visual Studio is installed to.</param>
		/// <returns>True if the directory was found, false otherwise.</returns>
		public static bool TryGetVSInstallDir(WindowsCompiler Compiler, [NotNullWhen(true)] out DirectoryReference? InstallDir)
		{
			if (BuildHostPlatform.Current.Platform != UnrealTargetPlatform.Win64)
			{
				InstallDir = null;
				return false;
			}

			List<VisualStudioInstallation> Installations = FindVisualStudioInstallations(Compiler);
			if(Installations.Count == 0)
			{
				InstallDir = null;
				return false;
			}
			else
			{
				InstallDir = Installations[0].BaseDir;
				return true;
			}
		}

		/// <summary>
		/// Read the Visual Studio install directory for the given compiler version. Note that it is possible for the compiler toolchain to be installed without
		/// Visual Studio.
		/// </summary>
		/// <param name="Compiler">Version of the toolchain to look for.</param>
		/// <returns>List of directories containing Visual Studio installations</returns>
		public static List<VisualStudioInstallation> FindVisualStudioInstallations(WindowsCompiler Compiler)
		{
			List<VisualStudioInstallation>? Installations;
			if(!CachedVisualStudioInstallations.TryGetValue(Compiler, out Installations))
			{
				Installations = new List<VisualStudioInstallation>();
			    if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			    {
				    if(Compiler == WindowsCompiler.VisualStudio2015_DEPRECATED)
				    {
					    // VS2015 just installs one toolchain; use that.
					    DirectoryReference? InstallDir;
					    if(MicrosoftPlatformSDK.TryReadInstallDirRegistryKey32("Microsoft\\VisualStudio\\SxS\\VS7", "14.0", out InstallDir))
					    {
						    Installations.Add(new VisualStudioInstallation(InstallDir));
					    }
				    }
				    else if(Compiler == WindowsCompiler.VisualStudio2017 || Compiler == WindowsCompiler.VisualStudio2019)
				    {
						try
						{
							SetupConfiguration Setup = new SetupConfiguration();
							IEnumSetupInstances Enumerator = Setup.EnumAllInstances();

							ISetupInstance[] Instances = new ISetupInstance[1];
							for(;;)
							{
								int NumFetched;
								Enumerator.Next(1, Instances, out NumFetched);

								if(NumFetched == 0)
								{
									break;
								}

								ISetupInstance2 Instance = (ISetupInstance2)Instances[0];
								if((Instance.GetState() & InstanceState.Local) == InstanceState.Local)
								{
									string VersionString = Instance.GetInstallationVersion();

									VersionNumber? Version;
									if (VersionNumber.TryParse(VersionString, out Version))
									{
										VersionNumber Version2019 = new VersionNumber(16);
										if(Compiler == WindowsCompiler.VisualStudio2019 && Version < Version2019)
										{
											continue;
										}
										else if(Compiler == WindowsCompiler.VisualStudio2017 && Version >= Version2019)
										{
											continue;
										}
									}

									ISetupInstanceCatalog? Catalog = Instance as ISetupInstanceCatalog;

									VisualStudioInstallation Installation = new VisualStudioInstallation(new DirectoryReference(Instance.GetInstallationPath()));
									if (Catalog != null && Catalog.IsPrerelease())
									{
										Installation.bPreview = true;
									}

									string ProductId = Instance.GetProduct().GetId();
									if (ProductId.Equals("Microsoft.VisualStudio.Product.WDExpress", StringComparison.Ordinal))
									{
										Installation.bExpress = true;
									}

									Log.TraceLog("Found Visual Studio installation: {0} (Product={1}, Version={2})", Instance.GetInstallationPath(), ProductId, VersionString);
									Installations.Add(Installation);
								}
							}
						}
						catch
						{
						}
					}
					else
				    {
					    throw new BuildException("Unsupported compiler version ({0})", Compiler);
				    }
				}
				CachedVisualStudioInstallations.Add(Compiler, Installations.OrderBy(x => !x.bExpress).ThenBy(x => !x.bPreview).ToList());
			}
			return Installations;
		}

		/// <summary>
		/// Determines the directory containing the MSVC toolchain
		/// </summary>
		/// <param name="Compiler">Major version of the compiler to use</param>
		/// <returns>Map of version number to directories</returns>
		static List<ToolChainInstallation> FindToolChainInstallations(WindowsCompiler Compiler)
		{
			List<ToolChainInstallation>? ToolChains;
			if(!CachedToolChainInstallations.TryGetValue(Compiler, out ToolChains))
			{
				ToolChains = new List<ToolChainInstallation>();
			    if (BuildHostPlatform.Current.Platform == UnrealTargetPlatform.Win64)
			    {
					if(Compiler == WindowsCompiler.Clang)
					{
						// Check for a manual installation to the default directory
						DirectoryReference ManualInstallDir = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.ProgramFiles)!, "LLVM");
						AddClangToolChain(ManualInstallDir, ToolChains);

						// Check for a manual installation to a custom directory
						string? LlvmPath = Environment.GetEnvironmentVariable("LLVM_PATH");
						if (!String.IsNullOrEmpty(LlvmPath))
						{
							AddClangToolChain(new DirectoryReference(LlvmPath), ToolChains);
						}

						// Check for installations bundled with Visual Studio 2019
						foreach (VisualStudioInstallation Installation in FindVisualStudioInstallations(WindowsCompiler.VisualStudio2019))
						{
							AddClangToolChain(DirectoryReference.Combine(Installation.BaseDir, "VC", "Tools", "Llvm"), ToolChains);
						}

						// Check for AutoSDK paths
						DirectoryReference AutoSdkDir;
						if(UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out AutoSdkDir))
						{
							DirectoryReference ClangBaseDir = DirectoryReference.Combine(AutoSdkDir, "Win64", "LLVM");
							if(DirectoryReference.Exists(ClangBaseDir))
							{
								foreach(DirectoryReference ToolChainDir in DirectoryReference.EnumerateDirectories(ClangBaseDir))
								{
									AddClangToolChain(ToolChainDir, ToolChains);
								}
							}
						}
					}
					else if(Compiler == WindowsCompiler.Intel)
					{
						// Just check for a manual installation
						DirectoryReference InstallDir = DirectoryReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.ProgramFilesX86)!, "IntelSWTools", "compilers_and_libraries", "windows");
						if(DirectoryReference.Exists(InstallDir))
						{
							FileReference IclPath = FileReference.Combine(InstallDir, "bin", "intel64", "icl.exe");
							if(FileReference.Exists(IclPath))
							{
								FileVersionInfo VersionInfo = FileVersionInfo.GetVersionInfo(IclPath.FullName);
								VersionNumber Version = new VersionNumber(VersionInfo.FileMajorPart, VersionInfo.FileMinorPart, VersionInfo.FileBuildPart);
								ToolChains.Add(new ToolChainInstallation(Version, 0, Version, true, false, null, InstallDir));
							}
						}
					}
				    else if(Compiler == WindowsCompiler.VisualStudio2015_DEPRECATED)
				    {
					    // VS2015 just installs one toolchain; use that.
					    List<VisualStudioInstallation> Installations = FindVisualStudioInstallations(Compiler);
					    foreach(VisualStudioInstallation Installation in Installations)
					    {
							DirectoryReference ToolChainBaseDir = DirectoryReference.Combine(Installation.BaseDir, "VC");
							if(IsValidToolChainDir2015(ToolChainBaseDir))
							{
								VersionNumber Version = new VersionNumber(14, 0);
								ToolChains.Add(new ToolChainInstallation(Version, 0, Version, Has64BitToolChain(ToolChainBaseDir), Installation.bPreview, null, ToolChainBaseDir));
							}
					    }
				    }
				    else if(Compiler == WindowsCompiler.VisualStudio2017 || Compiler == WindowsCompiler.VisualStudio2019)
				    {
						// Enumerate all the manually installed toolchains
						List<VisualStudioInstallation> Installations = FindVisualStudioInstallations(Compiler);
					    foreach(VisualStudioInstallation Installation in Installations)
					    {
						    DirectoryReference ToolChainBaseDir = DirectoryReference.Combine(Installation.BaseDir, "VC", "Tools", "MSVC");
							FindVisualStudioToolChains(ToolChainBaseDir, Installation.bPreview, ToolChains);
					    }

						// Enumerate all the AutoSDK toolchains
						DirectoryReference PlatformDir;
						if (UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out PlatformDir))
						{
							DirectoryReference ReleaseBaseDir = DirectoryReference.Combine(PlatformDir, "Win64", (Compiler == WindowsCompiler.VisualStudio2019) ? "VS2019" : "VS2017");
							FindVisualStudioToolChains(ReleaseBaseDir, false, ToolChains);

							DirectoryReference PreviewBaseDir = DirectoryReference.Combine(PlatformDir, "Win64", (Compiler == WindowsCompiler.VisualStudio2019) ? "VS2019-Preview" : "VS2017-Preview");
							FindVisualStudioToolChains(PreviewBaseDir, true, ToolChains);
						}
					}
					else
				    {
					    throw new BuildException("Unsupported compiler version ({0})", Compiler);
				    }
				}
				CachedToolChainInstallations.Add(Compiler, ToolChains);
			}
			return ToolChains;
		}

		/// <summary>
		/// Finds all the valid Visual Studio toolchains under the given base directory
		/// </summary>
		/// <param name="BaseDir">Base directory to search</param>
		/// <param name="bPreview">Whether this is a preview installation</param>
		/// <param name="ToolChains">Map of tool chain version to installation info</param>
		static void FindVisualStudioToolChains(DirectoryReference BaseDir, bool bPreview, List<ToolChainInstallation> ToolChains)
		{
			if (DirectoryReference.Exists(BaseDir))
			{
				foreach (DirectoryReference ToolChainDir in DirectoryReference.EnumerateDirectories(BaseDir))
				{
					VersionNumber? Version;
					if (IsValidToolChainDir2017or2019(ToolChainDir, out Version))
					{
						AddVisualCppToolChain(Version, bPreview, ToolChainDir, ToolChains);
					}
				}
			}
		}

		/// <summary>
		/// Add a Clang toolchain
		/// </summary>
		/// <param name="ToolChainDir"></param>
		/// <param name="ToolChains"></param>
		static void AddClangToolChain(DirectoryReference ToolChainDir, List<ToolChainInstallation> ToolChains)
		{
			FileReference CompilerFile = FileReference.Combine(ToolChainDir, "bin", "clang-cl.exe");
			if (FileReference.Exists(CompilerFile))
			{
				FileVersionInfo VersionInfo = FileVersionInfo.GetVersionInfo(CompilerFile.FullName);
				VersionNumber Version = new VersionNumber(VersionInfo.FileMajorPart, VersionInfo.FileMinorPart, VersionInfo.FileBuildPart);

				int Rank = PreferredClangVersions.TakeWhile(x => !x.Contains(Version)).Count();
				bool Is64Bit = Is64BitExecutable(CompilerFile);
				Log.TraceLog("Found Clang toolchain: {0} (Version={1}, Is64Bit={2}, Rank={3})", ToolChainDir, Version, Is64Bit, Rank);
				ToolChains.Add(new ToolChainInstallation(Version, Rank, Version, Is64Bit, false, null, ToolChainDir));
			}
		}

		/// <summary>
		/// Test whether an executable is 64-bit
		/// </summary>
		/// <param name="File">Executable to test</param>
		/// <returns></returns>
		static bool Is64BitExecutable(FileReference File)
		{
			using (FileStream Stream = new FileStream(File.FullName, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete))
			{
				byte[] Header = new byte[64];
				if (Stream.Read(Header, 0, Header.Length) != Header.Length)
				{
					return false;
				}
				if (Header[0] != (byte)'M' || Header[1] != (byte)'Z')
				{
					return false;
				}

				int Offset = BinaryPrimitives.ReadInt32LittleEndian(Header.AsSpan(0x3c));
				if (Stream.Seek(Offset, SeekOrigin.Begin) != Offset)
				{
					return false;
				}

				byte[] PeHeader = new byte[6];
				if(Stream.Read(PeHeader, 0, PeHeader.Length) != PeHeader.Length)
				{
					return false;
				}
				if (BinaryPrimitives.ReadUInt32BigEndian(PeHeader.AsSpan()) != 0x50450000)
				{
					return false;
				}

				ushort MachineType = BinaryPrimitives.ReadUInt16LittleEndian(PeHeader.AsSpan(4));

				const ushort IMAGE_FILE_MACHINE_AMD64 = 0x8664;
				return MachineType == IMAGE_FILE_MACHINE_AMD64;
			}
		}

		/// <summary>
		/// Adds a Visual C++ toolchain to a list of installations
		/// </summary>
		/// <param name="Version"></param>
		/// <param name="bPreview"></param>
		/// <param name="ToolChainDir"></param>
		/// <param name="ToolChains"></param>
		static void AddVisualCppToolChain(VersionNumber Version, bool bPreview, DirectoryReference ToolChainDir, List<ToolChainInstallation> ToolChains)
		{
			bool Is64Bit = Has64BitToolChain(ToolChainDir);

			VersionNumber? Family;
			if (!VersionNumber.TryParse(ToolChainDir.GetDirectoryName(), out Family))
			{
				Family = Version;
			}

			int FamilyRank = PreferredVisualCppVersions.TakeWhile(x => !x.Contains(Family)).Count();

			string? Error = null;
			if (Version >= new VersionNumber(14, 23, 0) && Version < new VersionNumber(14, 23, 28107))
			{
				Error = String.Format("The Visual C++ 14.23 toolchain is known to have code-generation issues with UE4. Please install a later toolchain from the Visual Studio installer. See here for more information: https://developercommunity.visualstudio.com/content/problem/734585/msvc-142328019-compilation-bug.html");
			}

			Log.TraceLog("Found Visual Studio toolchain: {0} (Family={1}, FamilyRank={2}, Version={3}, Is64Bit={4}, Preview={5}, Error={6})", ToolChainDir, Family, FamilyRank, Version, Is64Bit, bPreview, Error != null);
			ToolChains.Add(new ToolChainInstallation(Family, FamilyRank, Version, Is64Bit, bPreview, Error, ToolChainDir));
		}

		/// <summary>
		/// Checks if the given directory contains a valid Clang toolchain
		/// </summary>
		/// <param name="ToolChainDir">Directory to check</param>
		/// <returns>True if the given directory is valid</returns>
		static bool IsValidToolChainDirClang(DirectoryReference ToolChainDir)
		{
			return FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "clang-cl.exe"));
		}

		/// <summary>
		/// Checks if the given directory contains a valid Visual Studio 2015 toolchain
		/// </summary>
		/// <param name="ToolChainDir">Directory to check</param>
		/// <returns>True if the given directory is valid</returns>
		static bool IsValidToolChainDir2015(DirectoryReference ToolChainDir)
		{
			return FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "amd64", "cl.exe")) || FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "x86_amd64", "cl.exe"));
		}

		/// <summary>
		/// Determines if the given path is a valid Visual C++ version number
		/// </summary>
		/// <param name="ToolChainDir">The toolchain directory</param>
		/// <param name="Version">The version number for the toolchain</param>
		/// <returns>True if the path is a valid version</returns>
		static bool IsValidToolChainDir2017or2019(DirectoryReference ToolChainDir, [NotNullWhen(true)] out VersionNumber? Version)
		{
			FileReference CompilerExe = FileReference.Combine(ToolChainDir, "bin", "Hostx86", "x64", "cl.exe");
			if(!FileReference.Exists(CompilerExe))
			{
				CompilerExe = FileReference.Combine(ToolChainDir, "bin", "Hostx64", "x64", "cl.exe");
				if(!FileReference.Exists(CompilerExe))
				{
					Version = null;
					return false;
				}
			}

			FileVersionInfo VersionInfo = FileVersionInfo.GetVersionInfo(CompilerExe.FullName);
			if (VersionInfo.ProductMajorPart != 0)
			{
				Version = new VersionNumber(VersionInfo.ProductMajorPart, VersionInfo.ProductMinorPart, VersionInfo.ProductBuildPart);
				return true;
			}

			return VersionNumber.TryParse(ToolChainDir.GetDirectoryName(), out Version);
		}

		/// <summary>
		/// Checks if the given directory contains a 64-bit toolchain. Used to prefer regular Visual Studio versions over express editions.
		/// </summary>
		/// <param name="ToolChainDir">Directory to check</param>
		/// <returns>True if the given directory contains a 64-bit toolchain</returns>
		static bool Has64BitToolChain(DirectoryReference ToolChainDir)
		{
			return FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "amd64", "cl.exe")) || FileReference.Exists(FileReference.Combine(ToolChainDir, "bin", "Hostx64", "x64", "cl.exe"));
		}

		/// <summary>
		/// Determines if an IDE for the given compiler is installed.
		/// </summary>
		/// <param name="Compiler">Compiler to check for</param>
		/// <returns>True if the given compiler is installed</returns>
		public static bool HasIDE(WindowsCompiler Compiler)
		{
			return FindVisualStudioInstallations(Compiler).Count > 0;
		}

		/// <summary>
		/// Determines if a given compiler is installed
		/// </summary>
		/// <param name="Compiler">Compiler to check for</param>
		/// <returns>True if the given compiler is installed</returns>
		public static bool HasCompiler(WindowsCompiler Compiler)
		{
			return FindToolChainInstallations(Compiler).Count > 0;
		}

		/// <summary>
		/// Checks if a given Visual C++ toolchain version is compatible with UE4
		/// </summary>
		/// <param name="Version">The version number to check</param>
		/// <returns>True if the toolchain is compatible with UE4</returns>
		static bool IsCompatibleVisualCppToolChain(VersionNumber Version)
		{
			string? Message;
			return IsCompatibleVisualCppToolChain(Version, out Message);
		}

		/// <summary>
		/// Checks if a given Visual C++ toolchain version is compatible with UE4
		/// </summary>
		/// <param name="Version">The version number to check</param>
		/// <param name="Message">Receives a message describing why the toolchain is not compatible</param>
		/// <returns>True if the toolchain is compatible with UE4</returns>
		static bool IsCompatibleVisualCppToolChain(VersionNumber Version, [NotNullWhen(false)] out string? Message)
		{
			if (Version >= new VersionNumber(14, 23, 0) && Version < new VersionNumber(14, 23, 28107))
			{
				Message = String.Format("The Visual C++ 14.23 toolchain is known to have code-generation issues with UE4. Please install an earlier or later toolchain from the Visual Studio installer. See here for more information: https://developercommunity.visualstudio.com/content/problem/734585/msvc-142328019-compilation-bug.html");
				return false;
			}
			else
			{
				Message = null;
				return true;
			}
		}

		/// <summary>
		/// Select which toolchain to use, combining a custom preference with a default sort order
		/// </summary>
		/// <param name="ToolChains"></param>
		/// <param name="Preference"></param>
		/// <returns></returns>
		static ToolChainInstallation? SelectToolChain(IEnumerable<ToolChainInstallation> ToolChains, Func<IOrderedEnumerable<ToolChainInstallation>, IOrderedEnumerable<ToolChainInstallation>> Preference)
		{
			ToolChainInstallation? ToolChain = Preference(ToolChains.OrderByDescending(x => x.Error == null))
				.ThenByDescending(x => x.Is64Bit)
				.ThenBy(x => x.IsPreview)
				.ThenBy(x => x.FamilyRank)
				.ThenByDescending(x => x.Version)
				.FirstOrDefault();

			if (ToolChain?.Error != null)
			{
				throw new BuildException(ToolChain.Error);
			}

			return ToolChain;
		}

		/// <summary>
		/// Determines the directory containing the MSVC toolchain
		/// </summary>
		/// <param name="Compiler">Major version of the compiler to use</param>
		/// <param name="CompilerVersion">The minimum compiler version to use</param>
		/// <param name="OutToolChainVersion">Receives the chosen toolchain version</param>
		/// <param name="OutToolChainDir">Receives the directory containing the toolchain</param>
		/// <returns>True if the toolchain directory was found correctly</returns>
		public static bool TryGetToolChainDir(WindowsCompiler Compiler, string? CompilerVersion, [NotNullWhen(true)] out VersionNumber? OutToolChainVersion, [NotNullWhen(true)] out DirectoryReference? OutToolChainDir)
		{
			// Find all the installed toolchains
			List<ToolChainInstallation> ToolChains = FindToolChainInstallations(Compiler);

			// Figure out the actual version number that we want
			ToolChainInstallation? ToolChain = null;
			if (CompilerVersion == null)
			{
				ToolChain = SelectToolChain(ToolChains, x => x);
				if (ToolChain == null)
				{
					OutToolChainVersion = null;
					OutToolChainDir = null;
					return false;
				}
			}
			else if (String.Compare(CompilerVersion, "Latest", StringComparison.InvariantCultureIgnoreCase) == 0)
			{
				ToolChain = SelectToolChain(ToolChains, x => x.ThenByDescending(x => x.Version));
				if (ToolChain == null)
				{
					throw new BuildException("Unable to find C++ toolchain for {0}", Compiler);
				}
			}
			else if (String.Compare(CompilerVersion, "Preview", StringComparison.InvariantCultureIgnoreCase) == 0)
			{
				ToolChain = SelectToolChain(ToolChains, x => x.ThenByDescending(x => x.IsPreview));
				if (ToolChain == null || !ToolChain.IsPreview)
				{
					throw new BuildException("Unable to find preview toolchain for {0}", Compiler);
				}
			}
			else if (VersionNumber.TryParse(CompilerVersion, out VersionNumber? ToolChainVersion))
			{
				ToolChain = SelectToolChain(ToolChains, x => x.ThenByDescending(x => x.Version == ToolChainVersion).ThenByDescending(x => x.Family == ToolChainVersion));
				if (ToolChain == null)
				{
					throw new BuildException("Unable to find {0} toolchain for {1}", ToolChainVersion, Compiler);
				}
			}
			else
			{
				throw new BuildException("Unable to find {0} toolchain; '{1}' is an invalid version", GetCompilerName(Compiler), CompilerVersion);
			}

			// Get the actual directory for this version
			OutToolChainVersion = ToolChain.Version;
			OutToolChainDir = ToolChain.BaseDir;
			return true;
		}

		/// <summary>
		/// Gets the path to MSBuild. This mirrors the logic in GetMSBuildPath.bat.
		/// </summary>
		/// <param name="OutLocation">On success, receives the path to the MSBuild executable.</param>
		/// <returns>True on success.</returns>
		public static bool TryGetMsBuildPath([NotNullWhen(true)] out FileReference? OutLocation)
		{
			// Get the Visual Studio 2019 install directory
			List<DirectoryReference> InstallDirs2019 = WindowsPlatform.FindVisualStudioInstallations(WindowsCompiler.VisualStudio2019).ConvertAll(x => x.BaseDir);
			foreach (DirectoryReference InstallDir in InstallDirs2019)
			{
				FileReference MsBuildLocation = FileReference.Combine(InstallDir, "MSBuild", "Current", "Bin", "MSBuild.exe");
				if (FileReference.Exists(MsBuildLocation))
				{
					OutLocation = MsBuildLocation;
					return true;
				}
			}

			// Get the Visual Studio 2017 install directory
			List<DirectoryReference> InstallDirs2017 = WindowsPlatform.FindVisualStudioInstallations(WindowsCompiler.VisualStudio2017).ConvertAll(x => x.BaseDir);
			foreach (DirectoryReference InstallDir in InstallDirs2017)
			{
				FileReference MsBuildLocation = FileReference.Combine(InstallDir, "MSBuild", "15.0", "Bin", "MSBuild.exe");
				if(FileReference.Exists(MsBuildLocation))
				{
					OutLocation = MsBuildLocation;
					return true;
				}
			}

			// Try to get the MSBuild 14.0 path directly (see https://msdn.microsoft.com/en-us/library/hh162058(v=vs.120).aspx)
			FileReference? ToolPath = FileReference.Combine(DirectoryReference.GetSpecialFolder(Environment.SpecialFolder.ProgramFilesX86)!, "MSBuild", "14.0", "bin", "MSBuild.exe");
			if(FileReference.Exists(ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			} 

			// Check for older versions of MSBuild. These are registered as separate versions in the registry.
			if (TryReadMsBuildInstallPath("Microsoft\\MSBuild\\ToolsVersions\\14.0", "MSBuildToolsPath", "MSBuild.exe", out ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			}
			if (TryReadMsBuildInstallPath("Microsoft\\MSBuild\\ToolsVersions\\12.0", "MSBuildToolsPath", "MSBuild.exe", out ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			}
			if (TryReadMsBuildInstallPath("Microsoft\\MSBuild\\ToolsVersions\\4.0", "MSBuildToolsPath", "MSBuild.exe", out ToolPath))
			{
				OutLocation = ToolPath;
				return true;
			}

			OutLocation = null;
			return false;
		}

		/// <summary>
		/// Gets the MSBuild path, and throws an exception on failure.
		/// </summary>
		/// <returns>Path to MSBuild</returns>
		public static FileReference GetMsBuildToolPath()
		{
			FileReference? Location;
			if(!TryGetMsBuildPath(out Location))
			{
				throw new BuildException("Unable to find installation of MSBuild.");
			}
 			return Location;
		}

		public static string GetArchitectureSubpath(WindowsArchitecture arch)
		{
			string archPath = "Unknown";
			if (arch == WindowsArchitecture.x64)
			{
				archPath = "x64";
			}
			else if (arch == WindowsArchitecture.ARM64)
			{
				archPath = "arm64";
			}
			return archPath;
		}

		/// <summary>
		/// Function to query the registry under HKCU/HKLM Win32/Wow64 software registry keys for a certain install directory.
		/// This mirrors the logic in GetMSBuildPath.bat.
		/// </summary>
		/// <returns></returns>
		static bool TryReadMsBuildInstallPath(string KeyRelativePath, string KeyName, string MsBuildRelativePath, [NotNullWhen(true)] out FileReference? OutMsBuildPath)
		{
			string[] KeyBasePaths =
			{
				@"HKEY_CURRENT_USER\SOFTWARE\",
				@"HKEY_LOCAL_MACHINE\SOFTWARE\",
				@"HKEY_CURRENT_USER\SOFTWARE\Wow6432Node\",
				@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\"
			};

			foreach (string KeyBasePath in KeyBasePaths)
			{
				string? Value = Registry.GetValue(KeyBasePath + KeyRelativePath, KeyName, null) as string;
				if (Value != null)
				{
					FileReference MsBuildPath = FileReference.Combine(new DirectoryReference(Value), MsBuildRelativePath);
					if (FileReference.Exists(MsBuildPath))
					{
						OutMsBuildPath = MsBuildPath;
						return true;
					}
				}
			}

			OutMsBuildPath = null;
			return false;
		}

		/// <summary>
		/// Determines the directory containing the MSVC toolchain
		/// </summary>
		/// <param name="Compiler">Major version of the compiler to use</param>
		/// <returns>Map of version number to directories</returns>
		public static List<DirectoryReference> FindDiaSdkDirs(WindowsCompiler Compiler)
		{
			List<DirectoryReference>? DiaSdkDirs;
			if (!CachedDiaSdkDirs.TryGetValue(Compiler, out DiaSdkDirs))
			{
				DiaSdkDirs = new List<DirectoryReference>();

				DirectoryReference PlatformDir;
				if (UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out PlatformDir))
				{
					DirectoryReference DiaSdkDir = DirectoryReference.Combine(PlatformDir, "Win64", "DIA SDK", (Compiler == WindowsCompiler.VisualStudio2019) ? "VS2019" : "VS2017");
					if (IsValidDiaSdkDir(DiaSdkDir))
					{
						DiaSdkDirs.Add(DiaSdkDir);
					}
				}

				List<DirectoryReference> VisualStudioDirs = FindVisualStudioInstallations(Compiler).ConvertAll(x => x.BaseDir);
				foreach (DirectoryReference VisualStudioDir in VisualStudioDirs)
				{
					DirectoryReference DiaSdkDir = DirectoryReference.Combine(VisualStudioDir, "DIA SDK");
					if (IsValidDiaSdkDir(DiaSdkDir))
					{
						DiaSdkDirs.Add(DiaSdkDir);
					}
				}
			}
			return DiaSdkDirs;
		}

		/// <summary>
		/// Determines if a directory contains a valid DIA SDK
		/// </summary>
		/// <param name="DiaSdkDir">The directory to check</param>
		/// <returns>True if it contains a valid DIA SDK</returns>
		static bool IsValidDiaSdkDir(DirectoryReference DiaSdkDir)
		{
			return FileReference.Exists(FileReference.Combine(DiaSdkDir, "bin", "amd64", "msdia140.dll"));
		}



		public static bool TryGetWindowsSdkDir(string? DesiredVersion, [NotNullWhen(true)] out VersionNumber? OutSdkVersion, [NotNullWhen(true)] out DirectoryReference? OutSdkDir)
		{
			return MicrosoftPlatformSDK.TryGetWindowsSdkDir(DesiredVersion, out OutSdkVersion, out OutSdkDir);
		}


		/// <summary>
		/// Gets the platform name that should be used.
		/// </summary>
		public override string GetPlatformName()
		{
			return "Windows";
		}

		/// <summary>
		/// If this platform can be compiled with SN-DBS
		/// </summary>
		public override bool CanUseSNDBS()
		{
			return true;
		}

		/// <summary>
		/// If this platform can be compiled with FASTBuild
		/// </summary>
		public override bool CanUseFASTBuild()
		{
			return true;
		}

		/// <summary>
		/// Determines if the given name is a build product for a target.
		/// </summary>
		/// <param name="FileName">The name to check</param>
		/// <param name="NamePrefixes">Target or application names that may appear at the start of the build product name (eg. "UnrealEditor", "ShooterGameEditor")</param>
		/// <param name="NameSuffixes">Suffixes which may appear at the end of the build product name</param>
		/// <returns>True if the string matches the name of a build product, false otherwise</returns>
		public override bool IsBuildProduct(string FileName, string[] NamePrefixes, string[] NameSuffixes)
		{
			return IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".exe")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dll")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".dll.response")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".lib")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".pdb")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".exp")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".obj")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".map")
				|| IsBuildProductName(FileName, NamePrefixes, NameSuffixes, ".objpaths");
		}

		/// <summary>
		/// Get the extension to use for the given binary type
		/// </summary>
		/// <param name="InBinaryType"> The binrary type being built</param>
		/// <returns>string    The binary extenstion (ie 'exe' or 'dll')</returns>
		public override string GetBinaryExtension(UEBuildBinaryType InBinaryType)
		{
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
					return ".dll";
				case UEBuildBinaryType.Executable:
					return ".exe";
				case UEBuildBinaryType.StaticLibrary:
					return ".lib";
			}
			return base.GetBinaryExtension(InBinaryType);
		}

		/// <summary>
		/// Get the extensions to use for debug info for the given binary type
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <param name="InBinaryType"> The binary type being built</param>
		/// <returns>string[]    The debug info extensions (i.e. 'pdb')</returns>
		public override string[] GetDebugInfoExtensions(ReadOnlyTargetRules Target, UEBuildBinaryType InBinaryType)
		{
			switch (InBinaryType)
			{
				case UEBuildBinaryType.DynamicLinkLibrary:
				case UEBuildBinaryType.Executable:
					return new string[] {".pdb"};
			}
			return new string [] {};
		}

		public override bool HasDefaultBuildConfig(UnrealTargetPlatform Platform, DirectoryReference ProjectPath)
		{
			// check the base settings
			return base.HasDefaultBuildConfig(Platform, ProjectPath);
		}

		/// <summary>
		/// Modify the rules for a newly created module, where the target is a different host platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForOtherPlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
		}

		/// <summary>
		/// Gets the application icon for a given project
		/// </summary>
		/// <param name="ProjectFile">The project file</param>
		/// <returns>The icon to use for this project</returns>
		public static FileReference GetWindowsApplicationIcon(FileReference ProjectFile)
		{
			// Check if there's a custom icon
			if(ProjectFile != null)
			{
				FileReference IconFile = FileReference.Combine(ProjectFile.Directory, "Build", "Windows", "Application.ico");
				if(FileReference.Exists(IconFile))
				{
					return IconFile;
				}
			}

			// Otherwise use the default
			return FileReference.Combine(UnrealBuildTool.EngineDirectory, "Build", "Windows", "Resources", "Default.ico");
		}

		/// <summary>
		/// Gets the application icon for a given project
		/// </summary>
		/// <param name="ProjectFile">The project file</param>
		/// <returns>The icon to use for this project</returns>
		public virtual FileReference GetApplicationIcon(FileReference ProjectFile)
		{
			return GetWindowsApplicationIcon(ProjectFile);
		}


		/// <summary>
		/// Modify the rules for a newly created module, in a target that's being built for this platform.
		/// This is not required - but allows for hiding details of a particular platform.
		/// </summary>
		/// <param name="ModuleName">The name of the module</param>
		/// <param name="Rules">The module rules</param>
		/// <param name="Target">The target being build</param>
		public override void ModifyModuleRulesForActivePlatform(string ModuleName, ModuleRules Rules, ReadOnlyTargetRules Target)
		{
			bool bBuildShaderFormats = Target.bForceBuildShaderFormats;

			if (!Target.bBuildRequiresCookedData)
			{
				if (ModuleName == "TargetPlatform")
				{
					bBuildShaderFormats = true;
				}
			}

			// allow standalone tools to use target platform modules, without needing Engine
			if (ModuleName == "TargetPlatform")
			{
				if (Target.bForceBuildTargetPlatforms)
				{
					Rules.DynamicallyLoadedModuleNames.Add("WindowsTargetPlatform");
				}

				if (bBuildShaderFormats)
				{
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatD3D");
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatOpenGL");
					Rules.DynamicallyLoadedModuleNames.Add("ShaderFormatVectorVM");

					Rules.DynamicallyLoadedModuleNames.Remove("VulkanRHI");
					Rules.DynamicallyLoadedModuleNames.Add("VulkanShaderFormat");
				}
			}

			if (ModuleName == "D3D11RHI")
			{
				// To enable platform specific D3D11 RHI Types
				Rules.PrivateIncludePaths.Add("Runtime/Windows/D3D11RHI/Private/Windows");
			}

			if (ModuleName == "D3D12RHI")
			{
				if (Target.WindowsPlatform.bPixProfilingEnabled && Target.Configuration != UnrealTargetConfiguration.Shipping)
				{
					// Define to indicate profiling enabled (64-bit only)
					Rules.PublicDefinitions.Add("D3D12_PROFILING_ENABLED=1");
					Rules.PublicDefinitions.Add("PROFILE");
					Rules.PublicDependencyModuleNames.Add("WinPixEventRuntime");
				}
				else
				{
					Rules.PublicDefinitions.Add("D3D12_PROFILING_ENABLED=0");
				}
			}

			// Delay-load D3D12 so we can use the latest features and still run on downlevel versions of the OS
			Rules.PublicDelayLoadDLLs.Add("d3d12.dll");
		}

		/// <summary>
		/// Setup the target environment for building
		/// </summary>
		/// <param name="Target">Settings for the target being compiled</param>
		/// <param name="CompileEnvironment">The compile environment for this target</param>
		/// <param name="LinkEnvironment">The link environment for this target</param>
		public override void SetUpEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment CompileEnvironment, LinkEnvironment LinkEnvironment)
		{
			// @todo Remove this hack to work around broken includes
			CompileEnvironment.Definitions.Add("NDIS_MINIPORT_MAJOR_VERSION=0");

			CompileEnvironment.Definitions.Add("WIN32=1");
			if (Target.WindowsPlatform.bUseWindowsSDK10)
			{
				CompileEnvironment.Definitions.Add(String.Format("_WIN32_WINNT=0x{0:X4}", 0x0602));
				CompileEnvironment.Definitions.Add(String.Format("WINVER=0x{0:X4}", 0x0602));

			}
			else
			{
				CompileEnvironment.Definitions.Add(String.Format("_WIN32_WINNT=0x{0:X4}", Target.WindowsPlatform.TargetWindowsVersion));
				CompileEnvironment.Definitions.Add(String.Format("WINVER=0x{0:X4}", Target.WindowsPlatform.TargetWindowsVersion));
			}
			
			CompileEnvironment.Definitions.Add("PLATFORM_WINDOWS=1");
			CompileEnvironment.Definitions.Add("PLATFORM_MICROSOFT=1");

			// both Win32 and Win64 use Windows headers, so we enforce that here
			CompileEnvironment.Definitions.Add(String.Format("OVERRIDE_PLATFORM_HEADER_NAME={0}", GetPlatformName()));

			// Ray tracing only supported on 64-bit Windows.
			if (Target.Platform == UnrealTargetPlatform.Win64 && Target.WindowsPlatform.bEnableRayTracing)
			{
				CompileEnvironment.Definitions.Add("RHI_RAYTRACING=1");
			}

			// Add path to Intel math libraries when using ICL based on target platform
			if (Target.WindowsPlatform.Compiler == WindowsCompiler.Intel)
			{
				string Result = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "IntelSWTools", "compilers_and_libraries", "windows", "compiler", "lib", "intel64");
				if (!Directory.Exists(Result))
				{
					throw new BuildException("ICL was selected but the required math libraries were not found.  Could not find: " + Result);
				}

				LinkEnvironment.SystemLibraryPaths.Add(new DirectoryReference(Result));
			}

			// Explicitly exclude the MS C++ runtime libraries we're not using, to ensure other libraries we link with use the same
			// runtime library as the engine.
			bool bUseDebugCRT = Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT;
			if (!Target.bUseStaticCRT || bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("LIBCMT");
				LinkEnvironment.ExcludedLibraries.Add("LIBCPMT");
			}
			if (!Target.bUseStaticCRT || !bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("LIBCMTD");
				LinkEnvironment.ExcludedLibraries.Add("LIBCPMTD");
			}
			if (Target.bUseStaticCRT || bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("MSVCRT");
				LinkEnvironment.ExcludedLibraries.Add("MSVCPRT");
			}
			if (Target.bUseStaticCRT || !bUseDebugCRT)
			{
				LinkEnvironment.ExcludedLibraries.Add("MSVCRTD");
				LinkEnvironment.ExcludedLibraries.Add("MSVCPRTD");
			}
			LinkEnvironment.ExcludedLibraries.Add("LIBC");
			LinkEnvironment.ExcludedLibraries.Add("LIBCP");
			LinkEnvironment.ExcludedLibraries.Add("LIBCD");
			LinkEnvironment.ExcludedLibraries.Add("LIBCPD");

			//@todo ATL: Currently, only VSAccessor requires ATL (which is only used in editor builds)
			// When compiling games, we do not want to include ATL - and we can't when compiling games
			// made with Launcher build due to VS 2012 Express not including ATL.
			// If more modules end up requiring ATL, this should be refactored into a BuildTarget flag (bNeedsATL)
			// that is set by the modules the target includes to allow for easier tracking.
			// Alternatively, if VSAccessor is modified to not require ATL than we should always exclude the libraries.
			if (Target.LinkType == TargetLinkType.Monolithic &&
				(Target.Type == TargetType.Game || Target.Type == TargetType.Client || Target.Type == TargetType.Server))
			{
				LinkEnvironment.ExcludedLibraries.Add("atl");
				LinkEnvironment.ExcludedLibraries.Add("atls");
				LinkEnvironment.ExcludedLibraries.Add("atlsd");
				LinkEnvironment.ExcludedLibraries.Add("atlsn");
				LinkEnvironment.ExcludedLibraries.Add("atlsnd");
			}

			// Add the library used for the delayed loading of DLLs.
			LinkEnvironment.SystemLibraries.Add("delayimp.lib");

			//@todo - remove once FB implementation uses Http module
			if (Target.bCompileAgainstEngine)
			{
				// link against wininet (used by FBX and Facebook)
				LinkEnvironment.SystemLibraries.Add("wininet.lib");
			}

			// Compile and link with Win32 API libraries.
			LinkEnvironment.SystemLibraries.Add("rpcrt4.lib");
			//LinkEnvironment.AdditionalLibraries.Add("wsock32.lib");
			LinkEnvironment.SystemLibraries.Add("ws2_32.lib");
			LinkEnvironment.SystemLibraries.Add("dbghelp.lib");
			LinkEnvironment.SystemLibraries.Add("comctl32.lib");
			LinkEnvironment.SystemLibraries.Add("Winmm.lib");
			LinkEnvironment.SystemLibraries.Add("kernel32.lib");
			LinkEnvironment.SystemLibraries.Add("user32.lib");
			LinkEnvironment.SystemLibraries.Add("gdi32.lib");
			LinkEnvironment.SystemLibraries.Add("winspool.lib");
			LinkEnvironment.SystemLibraries.Add("comdlg32.lib");
			LinkEnvironment.SystemLibraries.Add("advapi32.lib");
			LinkEnvironment.SystemLibraries.Add("shell32.lib");
			LinkEnvironment.SystemLibraries.Add("ole32.lib");
			LinkEnvironment.SystemLibraries.Add("oleaut32.lib");
			LinkEnvironment.SystemLibraries.Add("uuid.lib");
			LinkEnvironment.SystemLibraries.Add("odbc32.lib");
			LinkEnvironment.SystemLibraries.Add("odbccp32.lib");
			LinkEnvironment.SystemLibraries.Add("netapi32.lib");
			LinkEnvironment.SystemLibraries.Add("iphlpapi.lib");
			LinkEnvironment.SystemLibraries.Add("setupapi.lib"); //  Required for access monitor device enumeration

			// Windows 7 Desktop Windows Manager API for Slate Windows Compliance
			LinkEnvironment.SystemLibraries.Add("dwmapi.lib");

			// IME
			LinkEnvironment.SystemLibraries.Add("imm32.lib");

			// For 64-bit builds, we'll forcibly ignore a linker warning with DirectInput.  This is
			// Microsoft's recommended solution as they don't have a fixed .lib for us.
			LinkEnvironment.AdditionalArguments += " /ignore:4078";

			// Set up default stack size
			LinkEnvironment.DefaultStackSize = Target.WindowsPlatform.DefaultStackSize;
			LinkEnvironment.DefaultStackSizeCommit = Target.WindowsPlatform.DefaultStackSizeCommit;

			LinkEnvironment.ModuleDefinitionFile = Target.WindowsPlatform.ModuleDefinitionFile;

			if (Target.bPGOOptimize || Target.bPGOProfile)
			{
				// LTCG is required for PGO
				//CompileEnvironment.bAllowLTCG = true;
				//LinkEnvironment.bAllowLTCG = true;

				CompileEnvironment.PGODirectory = Path.Combine(DirectoryReference.FromFile(Target.ProjectFile!).FullName, "Platforms", "Windows", "Build", "PGO");
				CompileEnvironment.PGOFilenamePrefix = string.Format("{0}-{1}-{2}", Target.Name, Target.Platform, Target.Configuration);

				LinkEnvironment.PGODirectory = CompileEnvironment.PGODirectory;
				LinkEnvironment.PGOFilenamePrefix = CompileEnvironment.PGOFilenamePrefix;
			}
		}

		/// <summary>
		/// Setup the configuration environment for building
		/// </summary>
		/// <param name="Target"> The target being built</param>
		/// <param name="GlobalCompileEnvironment">The global compile environment</param>
		/// <param name="GlobalLinkEnvironment">The global link environment</param>
		public override void SetUpConfigurationEnvironment(ReadOnlyTargetRules Target, CppCompileEnvironment GlobalCompileEnvironment, LinkEnvironment GlobalLinkEnvironment)
		{
			base.SetUpConfigurationEnvironment(Target, GlobalCompileEnvironment, GlobalLinkEnvironment);

			// NOTE: Even when debug info is turned off, we currently force the linker to generate debug info
			//       anyway on Visual C++ platforms.  This will cause a PDB file to be generated with symbols
			//       for most of the classes and function/method names, so that crashes still yield somewhat
			//       useful call stacks, even though compiler-generate debug info may be disabled.  This gives
			//       us much of the build-time savings of fully-disabled debug info, without giving up call
			//       data completely.
			GlobalLinkEnvironment.bCreateDebugInfo = true;
		}

		/// <summary>
		/// Whether this platform should create debug information or not
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>bool    true if debug info should be generated, false if not</returns>
		public override bool ShouldCreateDebugInfo(ReadOnlyTargetRules Target)
		{
			switch (Target.Configuration)
			{
				case UnrealTargetConfiguration.Development:
				case UnrealTargetConfiguration.Shipping:
				case UnrealTargetConfiguration.Test:
					return !Target.bOmitPCDebugInfoInDevelopment;
				case UnrealTargetConfiguration.DebugGame:
				case UnrealTargetConfiguration.Debug:
				default:
					return true;
			};
		}

		/// <summary>
		/// Creates a toolchain instance for the given platform.
		/// </summary>
		/// <param name="Target">The target being built</param>
		/// <returns>New toolchain instance.</returns>
		public override UEToolChain CreateToolChain(ReadOnlyTargetRules Target)
		{
			if (Target.WindowsPlatform.StaticAnalyzer == WindowsStaticAnalyzer.PVSStudio)
			{
				return new PVSToolChain(Target);
			}
			else
			{
				return new VCToolChain(Target);
			}
		}

		/// <summary>
		/// Allows the platform to return various build metadata that is not tracked by other means. If the returned string changes, the makefile will be invalidated.
		/// </summary>
		/// <param name="ProjectFile">The project file being built</param>
		/// <param name="Metadata">String builder to contain build metadata</param>
		public override void GetExternalBuildMetadata(FileReference? ProjectFile, StringBuilder Metadata)
		{
			base.GetExternalBuildMetadata(ProjectFile, Metadata);

			if(ProjectFile != null)
			{
				Metadata.AppendLine("ICON: {0}", GetApplicationIcon(ProjectFile));
			}
		}

		/// <summary>
		/// Deploys the given target
		/// </summary>
		/// <param name="Receipt">Receipt for the target being deployed</param>
		public override void Deploy(TargetReceipt Receipt)
		{
		}
	}

	class WindowsPlatformFactory : UEBuildPlatformFactory
	{
		public override UnrealTargetPlatform TargetPlatform
		{
			get { return UnrealTargetPlatform.Win64; }
		}

		/// <summary>
		/// Register the platform with the UEBuildPlatform class
		/// </summary>
		public override void RegisterBuildPlatforms()
		{
			MicrosoftPlatformSDK SDK = new MicrosoftPlatformSDK();

			// Register this build platform for Win64 (no more Win32)
			UEBuildPlatform.RegisterBuildPlatform(new WindowsPlatform(UnrealTargetPlatform.Win64, SDK));
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win64, UnrealPlatformGroup.Windows);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win64, UnrealPlatformGroup.Microsoft);
			UEBuildPlatform.RegisterPlatformWithGroup(UnrealTargetPlatform.Win64, UnrealPlatformGroup.Desktop);
		}
	}
}
