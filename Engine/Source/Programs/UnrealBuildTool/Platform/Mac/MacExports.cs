// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	/// <summary>
	/// Public Mac functions exposed to UAT
	/// </summary>
	public static class MacExports
	{
		/// <summary>
		/// Describes the architecture of the host. Note - this ignores translation.
		/// IsRunningUnderRosetta can be used to detect that we're running under translation
		/// </summary>
		public static UnrealArch HostArchitecture
		{
			get
			{
				return IsRunningOnAppleArchitecture ? UnrealArch.Arm64 : UnrealArch.X64;
			}
		}

		/// <summary>
		/// Cached result for AppleArch check
		/// </summary>
		private static bool? IsRunningOnAppleArchitectureVar;

		/// <summary>
		/// Cached result for Rosetta check
		/// </summary>
		private static bool? IsRunningUnderRosettaVar;

		/// <summary>
		/// Returns true if we're running under Rosetta 
		/// </summary>
		/// <returns></returns>
		public static bool IsRunningUnderRosetta
		{
			get
			{
				if (!IsRunningUnderRosettaVar.HasValue)
				{
					string TranslatedOutput = Utils.RunLocalProcessAndReturnStdOut("/usr/sbin/sysctl", "sysctl", null);
					IsRunningUnderRosettaVar = TranslatedOutput.Contains("sysctl.proc_translated: 1");
				}

				return IsRunningUnderRosettaVar.Value;
			}
		}

		/// <summary>
		/// Returns true if we're running on Apple architecture (either natively which mono/dotnet will do, or under Rosetta)
		/// </summary>
		/// <returns></returns>
		public static bool IsRunningOnAppleArchitecture
		{
			get
			{
				if (!IsRunningOnAppleArchitectureVar.HasValue)
				{
					// On an m1 mac this appears to be where the brand is.
					string BrandOutput = Utils.RunLocalProcessAndReturnStdOut("/usr/sbin/sysctl", "-n machdep.cpu.brand_string", null);
					IsRunningOnAppleArchitectureVar = BrandOutput.Contains("Apple") || IsRunningUnderRosetta;
				}

				return IsRunningOnAppleArchitectureVar.Value;
			}
		}

		private static bool bForceModernXcode = Environment.CommandLine.Contains("-modernxcode", StringComparison.OrdinalIgnoreCase);
		private static bool bForceLegacyXcode = Environment.CommandLine.Contains("-legacyxcode", StringComparison.OrdinalIgnoreCase);

		/// <summary>
		/// Is the current project using modern xcode?
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <returns></returns>
		public static bool UseModernXcode(FileReference? ProjectFile)
		{
			if (bForceModernXcode)
			{
				if (bForceLegacyXcode)
				{
					throw new BuildException("Both -modernxcode and -legacyxcode were specified, please use one or the other.");
				}
				return true;
			}
			if (bForceLegacyXcode)
			{
				return false;
			}

			// Modern Xcode mode does this now
			bool bUseModernXcode = false;
			if (OperatingSystem.IsMacOS())
			{
				ConfigHierarchy Ini = ConfigCache.ReadHierarchy(ConfigHierarchyType.Engine, ProjectFile?.Directory, UnrealTargetPlatform.Mac);
				Ini.TryGetValue("/Script/MacTargetPlatform.XcodeProjectSettings", "bUseModernXcode", out bUseModernXcode);
			}
			return bUseModernXcode;
		}

		/// <summary>
		/// Different ways that xcodebuild is run, so the scripts can behave appropriately
		/// </summary>
		public enum XcodeBuildMode
		{
			/// <summary>
			/// This is when hitting Build from in Xcode
			/// </summary>
			Default = 0,
			/// <summary>
			/// This runs after building when building on commandline directly with UBT
			/// </summary>
			PostBuildSync = 1,
			/// <summary>
			/// This runs when making a fully made .app in the Staged directory
			/// </summary>
			Stage = 2,
			/// <summary>
			/// This runs when packaging a full made .app into Project/Binaries
			/// </summary>
			Package = 3,
			/// <summary>
			/// This runs when packaging a .xcarchive for distribution
			/// </summary>
			Distribute = 4,
		}

		/// <summary>
		/// Generates a stub xcode project for the given project/platform/target combo, then builds or archives it
		/// </summary>
		/// <param name="ProjectFile">Project to build</param>
		/// <param name="Platform">Platform to build</param>
		/// <param name="Configuration">Configuration to build</param>
		/// <param name="TargetName">Target to build</param>
		/// <param name="BuildMode">Sets an envvar used inside the xcode project to control certain features</param>
		/// <param name="Logger"></param>
		/// <param name="ExtraOptions">Any extra options to pass to xcodebuild</param>
		/// <returns>xcode's exit code</returns>
		public static int BuildWithModernXcode(FileReference? ProjectFile, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, 
			string TargetName, XcodeBuildMode BuildMode, ILogger Logger, string ExtraOptions="")
		{
			DirectoryReference? GeneratedProjectFile;
			// we don't use distro flag when making a modern project
			IOSExports.GenerateRunOnlyXcodeProject(ProjectFile, Platform, TargetName, bForDistribution:false, Logger, out GeneratedProjectFile);

			if (GeneratedProjectFile == null)
			{
				return 1;
			}

			// run xcodebuild on the generated project to make the .app
			string XcodeBuildAction = BuildMode == XcodeBuildMode.Distribute ? "archive" : "build";
			return IOSExports.FinalizeAppWithModernXcode(GeneratedProjectFile!, Platform, TargetName, Configuration.ToString(),
				XcodeBuildAction, ExtraOptions + $" UE_XCODE_BUILD_MODE={BuildMode}", bForDistribution:false, Logger);
		}

		/// <summary>
		/// Strips symbols from a file
		/// </summary>
		/// <param name="SourceFile">The input file</param>
		/// <param name="TargetFile">The output file</param>
		/// <param name="Logger"></param>
		public static void StripSymbols(FileReference SourceFile, FileReference TargetFile, ILogger Logger)
		{
			MacToolChain ToolChain = new MacToolChain(null, ClangToolChainOptions.None, Logger);
			ToolChain.StripSymbols(SourceFile, TargetFile);
		}		
	}
}
