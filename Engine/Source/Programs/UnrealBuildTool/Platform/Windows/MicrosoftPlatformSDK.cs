// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Tools.DotNETCommon;
using System.Linq;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace UnrealBuildTool
{
	internal class MicrosoftPlatformSDK : UEBuildPlatformSDK
	{
		public override string GetMainVersion()
		{
			// the current Windows SDK version we expect
			return "10.0.18632";
		}

		public override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			MinVersion = "10.0.00000";
			MaxVersion = "10.9.99999";
		}

		public override void GetValidSoftwareVersionRange(out string MinVersion, out string MaxVersion)
		{
			// @todo we may want to split this for Win64 vs other things
			MinVersion = MaxVersion = null;
		}

		public override string GetInstalledSDKVersion()
		{
			if (Utils.IsRunningOnMono)
			{
				return null;
			}

			// @todo turnkey: do we support pre-10?

			// look for an installed SDK
			string Version = "v10.0";
			object Result = Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\" + Version, "InstallationFolder", null);
			if (Result != null)
			{
				return Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\" + Version, "ProductVersion", null) as string;
			}

			// look in AutoSDK location (note that it doesn't have a setup.bat, at least right now, so the AutoSDK system isn't used
			// @todo turnkey: make use of AutoSDKs, and maybe get rid of non-Latest style SDK selection in UEBUildWindows? (and move all that stuff here)
			DirectoryReference HostAutoSdkDir;
			if (TryGetHostPlatformAutoSDKDir(out HostAutoSdkDir))
			{
				DirectoryReference RootDirAutoSdk = DirectoryReference.Combine(HostAutoSdkDir, "Win64", "Windows Kits", "10", "include");
				if (DirectoryReference.Exists(RootDirAutoSdk))
				{
					// sort the directories under 10, and use the highest one (this logic mirrors UEBuildWindows logic)
					return DirectoryReference.EnumerateDirectories(RootDirAutoSdk).OrderBy(x => x.GetDirectoryName()).Last().GetDirectoryName();
				}
			}


			return null;
		}

		public override bool TryConvertVersionToInt(string StringValue, out UInt64 OutValue)
		{
			OutValue = 0;

			Match Result = Regex.Match(StringValue, @"^(\d+).(\d+).(\d+)");
			if (Result.Success)
			{
				// 8 bits for major, 8 for minor, 16 for patch
				OutValue |= UInt64.Parse(Result.Groups[1].Value) << 24;
				OutValue |= UInt64.Parse(Result.Groups[2].Value) << 16;
				OutValue |= UInt64.Parse(Result.Groups[3].Value) << 0;

				return true;
			}

			return false;
		}
	}
}
