// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Tools.DotNETCommon;

namespace UnrealBuildTool
{
	/// <summary>
	/// Public Linux functions exposed to UAT
	/// </summary>
	public static class WindowsExports
	{
		/// <summary>
		/// Tries to get the directory for an installed Visual Studio version
		/// </summary>
		/// <param name="Compiler">The compiler version</param>
		/// <param name="InstallDir">Receives the install directory on success</param>
		/// <returns>True if successful</returns>
		public static bool TryGetVSInstallDir(WindowsCompiler Compiler, out DirectoryReference InstallDir)
		{
			return WindowsPlatform.TryGetVSInstallDir(Compiler, out InstallDir);
		}

		/// <summary>
		/// Gets the path to MSBuild.exe
		/// </summary>
		/// <returns>Path to MSBuild.exe</returns>
		public static string GetMSBuildToolPath()
		{
			return WindowsPlatform.GetMsBuildToolPath().FullName;
		}

		/// <summary>
		/// Returns the common name of the current architecture
		/// </summary>
		/// <param name="arch">The architecture enum</param>
		/// <returns>String with the name</returns>
		public static string GetArchitectureSubpath(WindowsArchitecture arch)
		{
			return WindowsPlatform.GetArchitectureSubpath(arch);
		}

		/// <summary>
		/// Tries to get the directory for an installed Windows SDK
		/// </summary>
		/// <param name="DesiredVersion">Receives the desired version on success</param>
		/// <param name="OutSdkVersion">Version of SDK</param>
		/// <param name="OutSdkDir">Path to SDK root folder</param>
		/// <returns>String with the name</returns>
		public static bool TryGetWindowsSdkDir(string DesiredVersion, out Version OutSdkVersion, out DirectoryReference OutSdkDir)
		{
			VersionNumber vn;
			if(WindowsPlatform.TryGetWindowsSdkDir(DesiredVersion, out vn, out OutSdkDir))
			{
				OutSdkVersion = new Version(vn.ToString());
				return true;
			}
			OutSdkVersion = new Version();
			return false;
		}

		/// <summary>
		/// Gets a list of Windows Sdk installation directories, ordered by preference
		/// </summary>
		/// <returns>String with the name</returns>
		public static List<KeyValuePair<string, DirectoryReference>> GetWindowsSdkDirs()
		{
			List<KeyValuePair<string, DirectoryReference>> WindowsSdkDirs = new List<KeyValuePair<string, DirectoryReference>>();

			// Add the default directory first
			VersionNumber Version;
			DirectoryReference DefaultWindowsSdkDir;
			if (WindowsPlatform.TryGetWindowsSdkDir(null, out Version, out DefaultWindowsSdkDir))
			{
				WindowsSdkDirs.Add(new KeyValuePair<string, DirectoryReference>(Version.ToString(), DefaultWindowsSdkDir));
			}

			// Add all the other directories sorted in reverse order
			IReadOnlyDictionary<VersionNumber, DirectoryReference> WindowsSdkDirPairs = WindowsPlatform.FindWindowsSdkDirs();
			foreach(KeyValuePair<VersionNumber, DirectoryReference> Pair in WindowsSdkDirPairs.OrderByDescending(x => x.Key))
			{
				if(!WindowsSdkDirs.Any(x => x.Value == Pair.Value))
				{
					WindowsSdkDirs.Add(new KeyValuePair<string, DirectoryReference>(Pair.Key.ToString(), Pair.Value));
				}
			}

			return WindowsSdkDirs;
		}
	}
}
