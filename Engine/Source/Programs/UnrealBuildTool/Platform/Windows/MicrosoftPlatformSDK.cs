// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using EpicGames.Core;
using System.Linq;
using System.Text.RegularExpressions;
using Microsoft.Win32;
using System.Diagnostics.CodeAnalysis;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	internal class MicrosoftPlatformSDK : UEBuildPlatformSDK
	{
		/// <summary>
		/// The default Windows SDK version to be used, if installed.
		/// </summary>
		static readonly VersionNumber[] PreferredWindowsSdkVersions = new VersionNumber[]
		{
			VersionNumber.Parse("10.0.18362.0")
		};

		public override string GetMainVersion()
		{
			// preferred/main version is the top of the Preferred list - 
			return PreferredWindowsSdkVersions.First().ToString();
		}

		public override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			MinVersion = "10.0.00000.0";
			MaxVersion = "10.9.99999.0";
		}

		public override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			// minimum version is the oldest version in the Preferred list -
			MinVersion = PreferredWindowsSdkVersions.Min().ToString();
			MaxVersion = null;
		}

		public override string? GetInstalledSDKVersion()
		{
			if (!RuntimePlatform.IsWindows)
			{
				return null;
			}

			// use the PreferredWindowsSdkVersions array to find the SDK version that UBT will use to build with - 
			VersionNumber? WindowsVersion;
			if (TryGetWindowsSdkDir(null, out WindowsVersion, out _))
			{
				return WindowsVersion.ToString();
			}

			// if that failed, we aren't able to build, so give up
			return null;
		}

		public override bool TryConvertVersionToInt(string? StringValue, out UInt64 OutValue)
		{
			OutValue = 0;

			Match Result = Regex.Match(StringValue, @"^(\d+).(\d+).(\d+)");
			if (Result.Success)
			{
				// 8 bits for major, 8 for minor, 16 for build - we ignore patch (ie the 1234 in 10.0.14031.1234)
				OutValue |= UInt64.Parse(Result.Groups[1].Value) << 24;
				OutValue |= UInt64.Parse(Result.Groups[2].Value) << 16;
				OutValue |= UInt64.Parse(Result.Groups[3].Value) << 0;

				return true;
			}

			return false;
		}



		#region Windows Specific SDK discovery

		/// <summary>
		/// Cache of Windows SDK installation directories
		/// </summary>
		private static IReadOnlyDictionary<VersionNumber, DirectoryReference>? CachedWindowsSdkDirs;

		/// <summary>
		/// Cache of Universal CRT installation directories
		/// </summary>
		private static IReadOnlyDictionary<VersionNumber, DirectoryReference>? CachedUniversalCrtDirs;




		/// <summary>
		/// Updates the CachedWindowsSdkDirs and CachedUniversalCrtDirs variables
		/// </summary>
		private static void UpdateCachedWindowsSdks()
		{
			Dictionary<VersionNumber, DirectoryReference> WindowsSdkDirs = new Dictionary<VersionNumber, DirectoryReference>();
			Dictionary<VersionNumber, DirectoryReference> UniversalCrtDirs = new Dictionary<VersionNumber, DirectoryReference>();

			// Enumerate the Windows 8.1 SDK, if present
			DirectoryReference? InstallDir_8_1;
			if (TryReadInstallDirRegistryKey32("Microsoft\\Microsoft SDKs\\Windows\\v8.1", "InstallationFolder", out InstallDir_8_1))
			{
				if (FileReference.Exists(FileReference.Combine(InstallDir_8_1, "Include", "um", "windows.h")))
				{
					Log.TraceLog("Found Windows 8.1 SDK at {0}", InstallDir_8_1);
					VersionNumber Version_8_1 = new VersionNumber(8, 1);
					WindowsSdkDirs[Version_8_1] = InstallDir_8_1;
				}
			}

			// Find all the root directories for Windows 10 SDKs
			List<DirectoryReference> InstallDirs_10 = new List<DirectoryReference>();
			EnumerateSdkRootDirs(InstallDirs_10);

			// Enumerate all the Windows 10 SDKs
			foreach (DirectoryReference InstallDir_10 in InstallDirs_10.Distinct())
			{
				DirectoryReference IncludeRootDir = DirectoryReference.Combine(InstallDir_10, "Include");
				if (DirectoryReference.Exists(IncludeRootDir))
				{
					foreach (DirectoryReference IncludeDir in DirectoryReference.EnumerateDirectories(IncludeRootDir))
					{
						VersionNumber? IncludeVersion;
						if (VersionNumber.TryParse(IncludeDir.GetDirectoryName(), out IncludeVersion))
						{
							if (FileReference.Exists(FileReference.Combine(IncludeDir, "um", "windows.h")))
							{
								Log.TraceLog("Found Windows 10 SDK version {0} at {1}", IncludeVersion, InstallDir_10);
								WindowsSdkDirs[IncludeVersion] = InstallDir_10;
							}
							if (FileReference.Exists(FileReference.Combine(IncludeDir, "ucrt", "corecrt.h")))
							{
								Log.TraceLog("Found Universal CRT version {0} at {1}", IncludeVersion, InstallDir_10);
								UniversalCrtDirs[IncludeVersion] = InstallDir_10;
							}
						}
					}
				}
			}

			CachedWindowsSdkDirs = WindowsSdkDirs;
			CachedUniversalCrtDirs = UniversalCrtDirs;
		}

		/// <summary>
		/// Finds all the installed Windows SDK versions
		/// </summary>
		/// <returns>Map of version number to Windows SDK directories</returns>
		public static IReadOnlyDictionary<VersionNumber, DirectoryReference> FindWindowsSdkDirs()
		{
			// Update the cache of install directories, if it's not set
			if (CachedWindowsSdkDirs == null)
			{
				UpdateCachedWindowsSdks();
			}
			return CachedWindowsSdkDirs!;
		}

		/// <summary>
		/// Finds all the installed Universal CRT versions
		/// </summary>
		/// <returns>Map of version number to universal CRT directories</returns>
		public static IReadOnlyDictionary<VersionNumber, DirectoryReference> FindUniversalCrtDirs()
		{
			if (CachedUniversalCrtDirs == null)
			{
				UpdateCachedWindowsSdks();
			}
			return CachedUniversalCrtDirs!;
		}

		/// <summary>
		/// Determines the directory containing the Windows SDK toolchain
		/// </summary>
		/// <param name="DesiredVersion">The desired Windows SDK version. This may be "Latest", a specific version number, or null. If null, the function will look for DefaultWindowsSdkVersion. Failing that, it will return the latest version.</param>
		/// <param name="OutSdkVersion">Receives the version number of the selected Windows SDK</param>
		/// <param name="OutSdkDir">Receives the root directory for the selected SDK</param>
		/// <param name="MinVersion">Optional minimum required version. Ignored if DesiredVesrion is specified</param>
		/// <param name="MaxVersion">Optional maximum required version. Ignored if DesiredVesrion is specified</param>
		/// <returns>True if the toolchain directory was found correctly</returns>
		public static bool TryGetWindowsSdkDir(string? DesiredVersion, [NotNullWhen(true)] out VersionNumber? OutSdkVersion, [NotNullWhen(true)] out DirectoryReference? OutSdkDir, VersionNumber? MinVersion = null, VersionNumber? MaxVersion = null)
		{
			// Get a map of Windows SDK versions to their root directories
			/*IReadOnlyDictionary<VersionNumber, DirectoryReference> WindowsSdkDirs =*/
			FindWindowsSdkDirs();

			// Figure out which version number to look for
			VersionNumber? WindowsSdkVersion = null;
			if (!string.IsNullOrEmpty(DesiredVersion))
			{
				if (String.Compare(DesiredVersion, "Latest", StringComparison.InvariantCultureIgnoreCase) == 0 && CachedWindowsSdkDirs!.Count > 0)
				{
					WindowsSdkVersion = CachedWindowsSdkDirs.OrderBy(x => x.Key).Last().Key;
				}
				else if (!VersionNumber.TryParse(DesiredVersion, out WindowsSdkVersion))
				{
					throw new BuildException("Unable to find requested Windows SDK; '{0}' is an invalid version", DesiredVersion);
				}
			}
			else if (MinVersion == null && MaxVersion == null)
			{
				WindowsSdkVersion = PreferredWindowsSdkVersions.FirstOrDefault(x => CachedWindowsSdkDirs!.ContainsKey(x));
				if (WindowsSdkVersion == null && CachedWindowsSdkDirs!.Count > 0)
				{
					WindowsSdkVersion = CachedWindowsSdkDirs.OrderBy(x => x.Key).Last().Key;
				}
			}
			else if (CachedWindowsSdkDirs!.Count > 0)
			{
				WindowsSdkVersion = CachedWindowsSdkDirs.OrderBy(x => x.Key).Where( 
					x =>
					(MinVersion == null || x.Key >= MinVersion) &&
					(MaxVersion == null || x.Key <= MaxVersion)).Last().Key;
			}

			// Get the actual directory for this version
			DirectoryReference? SdkDir;
			if (WindowsSdkVersion != null && CachedWindowsSdkDirs!.TryGetValue(WindowsSdkVersion, out SdkDir))
			{
				OutSdkDir = SdkDir;
				OutSdkVersion = WindowsSdkVersion;
				return true;
			}
			else
			{
				OutSdkDir = null;
				OutSdkVersion = null;
				return false;
			}
		}

		/// <summary>
		/// Gets the installation directory for the NETFXSDK
		/// </summary>
		/// <param name="OutInstallDir">Receives the installation directory on success</param>
		/// <returns>True if the directory was found, false otherwise</returns>
		public static bool TryGetNetFxSdkInstallDir([NotNullWhen(true)] out DirectoryReference? OutInstallDir)
		{
			DirectoryReference? HostAutoSdkDir;
			if (UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out HostAutoSdkDir))
			{
				DirectoryReference NetFxDir_4_6 = DirectoryReference.Combine(HostAutoSdkDir, "Win64", "Windows Kits", "NETFXSDK", "4.6");
				if (FileReference.Exists(FileReference.Combine(NetFxDir_4_6, "Include", "um", "mscoree.h")))
				{
					OutInstallDir = NetFxDir_4_6;
					return true;
				}

				DirectoryReference NetFxDir_4_6_1 = DirectoryReference.Combine(HostAutoSdkDir, "Win64", "Windows Kits", "NETFXSDK", "4.6.1");
				if (FileReference.Exists(FileReference.Combine(NetFxDir_4_6_1, "Include", "um", "mscoree.h")))
				{
					OutInstallDir = NetFxDir_4_6_1;
					return true;
				}
			}

			string NetFxSDKKeyName = "Microsoft\\Microsoft SDKs\\NETFXSDK";
			string[] PreferredVersions = new string[] { "4.6.2", "4.6.1", "4.6" };
			foreach (string PreferredVersion in PreferredVersions)
			{
				if (TryReadInstallDirRegistryKey32(NetFxSDKKeyName + "\\" + PreferredVersion, "KitsInstallationFolder", out OutInstallDir))
				{
					return true;
				}
			}

			// If we didn't find one of our preferred versions for NetFXSDK, use the max version present on the system
			Version? MaxVersion = null;
			string? MaxVersionString = null;
			foreach (string ExistingVersionString in ReadInstallDirSubKeys32(NetFxSDKKeyName))
			{
				Version? ExistingVersion;
				if (!Version.TryParse(ExistingVersionString, out ExistingVersion))
				{
					continue;
				}
				if (MaxVersion == null || ExistingVersion.CompareTo(MaxVersion) > 0)
				{
					MaxVersion = ExistingVersion;
					MaxVersionString = ExistingVersionString;
				}
			}

			if (MaxVersionString != null)
			{
				return TryReadInstallDirRegistryKey32(NetFxSDKKeyName + "\\" + MaxVersionString, "KitsInstallationFolder", out OutInstallDir);
			}

			OutInstallDir = null;
			return false;
		}


		static readonly KeyValuePair<RegistryKey, string>[] InstallDirRoots = {
			new KeyValuePair<RegistryKey, string>(Registry.CurrentUser, "SOFTWARE\\"),
			new KeyValuePair<RegistryKey, string>(Registry.LocalMachine, "SOFTWARE\\"),
			new KeyValuePair<RegistryKey, string>(Registry.CurrentUser, "SOFTWARE\\Wow6432Node\\"),
			new KeyValuePair<RegistryKey, string>(Registry.LocalMachine, "SOFTWARE\\Wow6432Node\\")
		};

		/// <summary>
		/// Reads an install directory for a 32-bit program from a registry key. This checks for per-user and machine wide settings, and under the Wow64 virtual keys (HKCU\SOFTWARE, HKLM\SOFTWARE, HKCU\SOFTWARE\Wow6432Node, HKLM\SOFTWARE\Wow6432Node).
		/// </summary>
		/// <param name="KeySuffix">Path to the key to read, under one of the roots listed above.</param>
		/// <param name="ValueName">Value to be read.</param>
		/// <param name="InstallDir">On success, the directory corresponding to the value read.</param>
		/// <returns>True if the key was read, false otherwise.</returns>
		public static bool TryReadInstallDirRegistryKey32(string KeySuffix, string ValueName, [NotNullWhen(true)] out DirectoryReference? InstallDir)
		{
			foreach (KeyValuePair<RegistryKey, string> InstallRoot in InstallDirRoots)
			{
				using (RegistryKey Key = InstallRoot.Key.OpenSubKey(InstallRoot.Value + KeySuffix))
				{
					if (Key != null && TryReadDirRegistryKey(Key.Name, ValueName, out InstallDir))
					{
						return true;
					}
				}
			}
			InstallDir = null;
			return false;
		}

		/// <summary>
		/// For each root location relevant to install dirs, look for the given key and add its subkeys to the set of subkeys to return.
		/// This checks for per-user and machine wide settings, and under the Wow64 virtual keys (HKCU\SOFTWARE, HKLM\SOFTWARE, HKCU\SOFTWARE\Wow6432Node, HKLM\SOFTWARE\Wow6432Node).
		/// </summary>
		/// <param name="KeyName">The subkey to look for under each root location</param>
		/// <returns>A list of unique subkeys found under any of the existing subkeys</returns>
		static string[] ReadInstallDirSubKeys32(string KeyName)
		{
			HashSet<string> AllSubKeys = new HashSet<string>(StringComparer.Ordinal);
			foreach (KeyValuePair<RegistryKey, string> Root in InstallDirRoots)
			{
				using (RegistryKey Key = Root.Key.OpenSubKey(Root.Value + KeyName))
				{
					if (Key == null)
					{
						continue;
					}

					foreach (string SubKey in Key.GetSubKeyNames())
					{
						AllSubKeys.Add(SubKey);
					}
				}
			}
			return AllSubKeys.ToArray();
		}

		/// <summary>
		/// Attempts to reads a directory name stored in a registry key
		/// </summary>
		/// <param name="KeyName">Key to read from</param>
		/// <param name="ValueName">Value within the key to read</param>
		/// <param name="Value">The directory read from the registry key</param>
		/// <returns>True if the key was read, false if it was missing or empty</returns>
		static bool TryReadDirRegistryKey(string KeyName, string ValueName, [NotNullWhen(true)] out DirectoryReference? Value)
		{
			string? StringValue = Registry.GetValue(KeyName, ValueName, null) as string;
			if (String.IsNullOrEmpty(StringValue))
			{
				Value = null;
				return false;
			}
			else
			{
				Value = new DirectoryReference(StringValue);
				return true;
			}
		}

		/// <summary>
		/// Enumerates all the Windows 10 SDK root directories
		/// </summary>
		/// <param name="RootDirs">Receives all the Windows 10 sdk root directories</param>
		public static void EnumerateSdkRootDirs(List<DirectoryReference> RootDirs)
		{
			DirectoryReference? RootDir;
			if (TryReadInstallDirRegistryKey32("Microsoft\\Windows Kits\\Installed Roots", "KitsRoot10", out RootDir))
			{
				Log.TraceLog("Found Windows 10 SDK root at {0} (1)", RootDir);
				RootDirs.Add(RootDir);
			}
			if (TryReadInstallDirRegistryKey32("Microsoft\\Microsoft SDKs\\Windows\\v10.0", "InstallationFolder", out RootDir))
			{
				Log.TraceLog("Found Windows 10 SDK root at {0} (2)", RootDir);
				RootDirs.Add(RootDir);
			}

			DirectoryReference? HostAutoSdkDir;
			if (UEBuildPlatformSDK.TryGetHostPlatformAutoSDKDir(out HostAutoSdkDir))
			{
				DirectoryReference RootDirAutoSdk = DirectoryReference.Combine(HostAutoSdkDir, "Win64", "Windows Kits", "10");
				if (DirectoryReference.Exists(RootDirAutoSdk))
				{
					Log.TraceLog("Found Windows 10 AutoSDK root at {0}", RootDirAutoSdk);
					RootDirs.Add(RootDirAutoSdk);
				}
			}
		}

		#endregion
	}
}
