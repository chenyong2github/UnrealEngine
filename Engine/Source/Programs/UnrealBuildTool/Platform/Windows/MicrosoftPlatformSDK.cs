// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Tools.DotNETCommon;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace UnrealBuildTool
{
	internal class MicrosoftPlatformSDK : UEBuildPlatformSDK
	{
		public override string GetMainVersion()
		{
			// the current and previous versions of the SDK (technically, NX Add-On), with appended NEX version, we support
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
			// @todo turnkey: MicrosoftPlatformSDK maybe?
			string Version = "v10.0";
			object Result = Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\" + Version, "InstallationFolder", null);

			if (Result != null)
			{
				return Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Microsoft SDKs\Windows\" + Version, "ProductVersion", null) as string;
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
