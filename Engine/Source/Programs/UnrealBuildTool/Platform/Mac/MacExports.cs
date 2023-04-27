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

		/// <summary>
		/// Is the current project using modern xcode?
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <returns></returns>
		public static bool UseModernXcode(FileReference? ProjectFile)
		{
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
		/// Generates a stub xcode project for the given project/platform/target combo, then builds or archives it
		/// </summary>
		/// <param name="ProjectFile"></param>
		/// <param name="Platform"></param>
		/// <param name="Configuration"></param>
		/// <param name="TargetName"></param>
		/// <param name="bArchiveForDistro"></param>
		/// <param name="Logger"></param>
		/// <returns></returns>
		public static bool BuildWithModernXcode(FileReference? ProjectFile, UnrealTargetPlatform Platform, UnrealTargetConfiguration Configuration, 
			string TargetName, bool bArchiveForDistro, ILogger Logger)
		{
			DirectoryReference? GeneratedProjectFile;
			// we don't use distro flag when making a modern project
			IOSExports.GenerateRunOnlyXcodeProject(ProjectFile, Platform, TargetName, bForDistribution:false, Logger, out GeneratedProjectFile);

			if (GeneratedProjectFile == null)
			{
				return false;
			}

			// run xcodebuild on the generated project to make the .app
			IOSExports.FinalizeAppWithModernXcode(GeneratedProjectFile!, Platform, TargetName, Configuration.ToString(),
				bArchiveForDistro ? "archive" : "build", "", bArchiveForDistro, Logger);

			return true;
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
