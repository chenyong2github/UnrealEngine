// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Diagnostics;
using EpicGames.Core;

namespace AutomationTool
{
	/// <summary>
	/// Host platform abstraction
	/// </summary>
	public abstract class HostPlatform
	{
		/// <summary>
		/// Current running host platform.
		/// </summary>
		public static readonly HostPlatform Current = Initialize();

		/// <summary>
		/// Initializes the current platform.
		/// </summary>
		private static HostPlatform Initialize()
		{
			switch (RuntimePlatform.Current)
			{
				case RuntimePlatform.Type.Windows: return new WindowsHostPlatform();
				case RuntimePlatform.Type.Mac:     return new MacHostPlatform();
				case RuntimePlatform.Type.Linux:   return new LinuxHostPlatform();
			}
			throw new Exception ("Unhandled runtime platform " + Environment.OSVersion.Platform);
		}

		/// <summary>
		/// Gets the build executable filename for NET Framework projects e.g. msbuild, or xbuild
		/// </summary>
		/// <returns></returns>
		abstract public string GetFrameworkMsbuildExe();

		/// <summary>
		/// Gets the path to dotnet
		/// </summary>
		/// <returns></returns>
		abstract public FileReference GetDotnetExe();

		/// <summary>
		/// Gets the build executable filename for NET Core projects. Typically, the path to the bundled dotnet executable.
		/// </summary>
		/// <returns></returns>
		abstract public string GetDotnetMsbuildExe();

		/// <summary>
		/// Folder under UE/ to the platform's binaries.
		/// </summary>
		abstract public string RelativeBinariesFolder { get; }

		/// <summary>
		/// Full path to the UnrealEditor executable for the current platform.
		/// </summary>
		/// <param name="UnrealExe"></param>
		/// <returns></returns>
		abstract public string GetUnrealExePath(string UnrealExe);

		[Obsolete("Deprecated in 5.0; use GetUnrealExePath() instead")]
		public string GetUE4ExePath(string UE4Exe)
		{
			return GetUnrealExePath(UE4Exe);
		}

		/// <summary>
		/// Log folder for local builds.
		/// </summary>
		abstract public string LocalBuildsLogFolder { get; }

		/// <summary>
		/// Name of the p4 executable.
		/// </summary>
		abstract public string P4Exe { get; }

		/// <summary>
		/// Creates a process and sets it up for the current platform.
		/// </summary>
		/// <param name="LogName"></param>
		/// <returns></returns>
		abstract public Process CreateProcess(string AppName);

		/// <summary>
		/// Sets any additional options for running an executable.
		/// </summary>
		/// <param name="AppName"></param>
		/// <param name="Options"></param>
		/// <param name="CommandLine"></param>
		abstract public void SetupOptionsForRun(ref string AppName, ref CommandUtils.ERunOptions Options, ref string CommandLine);

		/// <summary>
		/// Sets the console control handler for the current platform.
		/// </summary>
		/// <param name="Handler"></param>
		abstract public void SetConsoleCtrlHandler(ProcessManager.CtrlHandlerDelegate Handler);

		/// <summary>
		/// Returns the type of the host editor platform.
		/// </summary>
		abstract public UnrealBuildTool.UnrealTargetPlatform HostEditorPlatform { get; }

		/// <summary>
		/// Returns the pdb file extenstion for the host platform.
		/// </summary>
		abstract public string PdbExtension { get; }

		/// <summary>
		/// List of processes that can't not be killed
		/// </summary>
		abstract public string[] DontKillProcessList { get; }
	}
}
