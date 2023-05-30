// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	/// <summary>
	/// Public Linux functions exposed to UAT
	/// </summary>
	public class LinuxExports
	{
		/// <summary>
		/// 
		/// </summary>
		/// <param name="SourceFile"></param>
		/// <param name="TargetFile"></param>
		/// <param name="Logger">Logger for output</param>
		public static void StripSymbols(FileReference SourceFile, FileReference TargetFile, ILogger Logger)
		{
			LinuxToolChain ToolChain = new LinuxToolChain(LinuxPlatform.DefaultHostArchitecture, new LinuxPlatformSDK(Logger), ClangToolChainOptions.None, Logger);
			ToolChain.StripSymbols(SourceFile, TargetFile, Logger);
		}
	}
}
