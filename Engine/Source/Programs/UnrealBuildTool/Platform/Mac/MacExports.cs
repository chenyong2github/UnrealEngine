// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;

#nullable disable

namespace UnrealBuildTool
{
	/// <summary>
	/// Public Mac functions exposed to UAT
	/// </summary>
	public static class MacExports
	{
		/// <summary>
		/// Intel architecture string as described by clang and other tools
		/// </summary>
		public static string IntelArchitecture = "x86_64";

		/// <summary>
		/// Apple silicon architecture string as described by clang and other tools
		/// </summary>
		public static string AppleArchitecture = "arm64";

		/// <summary>
		/// Describes the architecture of the host. Note - this ignores translation.
		/// IsRunningUnderRosetta can be used to detect that we're running under translation
		/// </summary>
		public static string HostArchitecture
		{
			get
			{
				return IsRunningOnAppleArchitecture ? AppleArchitecture : IntelArchitecture;
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
					string TranslatedOutput = Utils.RunLocalProcessAndReturnStdOut("/usr/sbin/sysctl", "sysctl");
					IsRunningUnderRosettaVar = TranslatedOutput.StartsWith("sysctl.proc_translated: 1");
				}

				return IsRunningUnderRosettaVar.Value;
			}
		}

		/// <summary>
		/// Returns true if we're running on Apple architecture (either natively which mono will do, or under Rosetta)
		/// </summary>
		/// <returns></returns>
		public static bool IsRunningOnAppleArchitecture
		{
			get
			{
				if (!IsRunningOnAppleArchitectureVar.HasValue)
				{
					// On an m1 mac this appears to be where the brand is.
					string BrandOutput = Utils.RunLocalProcessAndReturnStdOut("/usr/sbin/sysctl", "-n machdep.cpu.brand_string");
					IsRunningOnAppleArchitectureVar = BrandOutput.StartsWith("Apple") || IsRunningUnderRosetta;
				}

				return IsRunningOnAppleArchitectureVar.Value;
			}
		}

		/// <summary>
		/// Strips symbols from a file
		/// </summary>
		/// <param name="SourceFile">The input file</param>
		/// <param name="TargetFile">The output file</param>
		public static void StripSymbols(FileReference SourceFile, FileReference TargetFile)
		{
			MacToolChain ToolChain = new MacToolChain(null, MacToolChainOptions.None);
			ToolChain.StripSymbols(SourceFile, TargetFile);
		}		
	}
}
