// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using Tools.DotNETCommon;
using System.Text.RegularExpressions;
using Microsoft.Win32;
using System.Diagnostics;

namespace UnrealBuildTool
{
	internal class ApplePlatformSDK : UEBuildPlatformSDK
	{
		public override string GetMainVersion()
		{
			return "11.5";
		}

		public override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			if (IsMac())
			{
				MinVersion = "11.0.0";
				MaxVersion = "12.9.9";
			}
			else
			{
				// @todo turnkey: these are MobileDevice .dll versions in Windows - to get the iTunes app version (12.3.4.1 etc) would need to hunt down the .exe
				MinVersion = "1100.0.0.0";
				MaxVersion = "1299.0";
			}
		}


		public override string GetInstalledSDKVersion()
		{
			// get xcode version on Mac
			if (IsMac())
			{
				string Output = Utils.RunLocalProcessAndReturnStdOut("sh", "-c 'xcodebuild -version'");
				Match Result = Regex.Match(Output, @"Xcode (\S*)");
				return Result.Success ? Result.Groups[1].Value : "";
			}

			// otherwise, get iTunes "Version"
			string DllPath = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Apple Inc.\\Apple Mobile Device Support\\Shared", "iTunesMobileDeviceDLL", null) as string;
			if (string.IsNullOrEmpty(DllPath) || !File.Exists(DllPath))
			{
				DllPath = Registry.GetValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Wow6432Node\\Apple Inc.\\Apple Mobile Device Support\\Shared", "MobileDeviceDLL", null) as string;
				if (string.IsNullOrEmpty(DllPath) || !File.Exists(DllPath))
				{
					DllPath = FindWindowsStoreITunesDLL();

				}
			}

			if (!string.IsNullOrEmpty(DllPath) && File.Exists(DllPath))
			{
				return FileVersionInfo.GetVersionInfo(DllPath).FileVersion;
			}

			return null;
		}

		public override bool TryConvertVersionToInt(string StringValue, out UInt64 OutValue)
		{
			// 8 bits per component, with high getting extra from high 32
			Match Result = Regex.Match(StringValue, @"^(\d+).(\d+)(.(\d+))?(.(\d+))?$");
			if (Result.Success)
			{
				OutValue = UInt64.Parse(Result.Groups[1].Value) << 24 | UInt64.Parse(Result.Groups[2].Value) << 16;
				if (Result.Groups[4].Success)
				{
					OutValue |= UInt64.Parse(Result.Groups[4].Value) << 8;
				}
				if (Result.Groups[6].Success)
				{
					OutValue |= UInt64.Parse(Result.Groups[6].Value) << 0;
				}
				return true;
			}
			OutValue = 0;
			return false;
		}





		static string FindWindowsStoreITunesDLL()
		{
			string InstallPath = null;

			string PackagesKeyName = "Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\PackageRepository\\Packages";

			RegistryKey PackagesKey = Registry.LocalMachine.OpenSubKey(PackagesKeyName);
			if (PackagesKey != null)
			{
				string[] PackageSubKeyNames = PackagesKey.GetSubKeyNames();

				foreach (string PackageSubKeyName in PackageSubKeyNames)
				{
					if (PackageSubKeyName.Contains("AppleInc.iTunes") && (PackageSubKeyName.Contains("_x64") || PackageSubKeyName.Contains("_x86")))
					{
						string FullPackageSubKeyName = PackagesKeyName + "\\" + PackageSubKeyName;

						RegistryKey iTunesKey = Registry.LocalMachine.OpenSubKey(FullPackageSubKeyName);
						if (iTunesKey != null)
						{
							InstallPath = (string)iTunesKey.GetValue("Path") + "\\AMDS32\\MobileDevice.dll";
							break;
						}
					}
				}
			}

			return InstallPath;
		}
	}
}
