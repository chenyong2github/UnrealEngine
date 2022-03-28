// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using EpicGames.Core;
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
			if (RuntimePlatform.IsMac)
			{
				MinVersion = "11.0.0";
				MaxVersion = "13.9.9";
			}
			else
			{
				// @todo turnkey: these are MobileDevice .dll versions in Windows - to get the iTunes app version (12.3.4.1 etc) would need to hunt down the .exe
				MinVersion = "1100.0.0.0";
				MaxVersion = "1399.0";
			}
		}

		public override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}

		public override bool TryConvertVersionToInt(string? StringValue, out UInt64 OutValue)
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

		public override string? GetInstalledSDKVersion()
		{
			return UnrealBuildBase.ApplePlatformSDK.InstalledSDKVersion;
		}

		protected override SDKStatus HasRequiredManualSDKInternal()
		{
			SDKStatus Status = base.HasRequiredManualSDKInternal();

			// iTunes is technically only need to deploy to and run on connected devices.
			// This code removes requirement for Windows builders to have Xcode installed.
			if (Status == SDKStatus.Invalid && !RuntimePlatform.IsMac && Environment.GetEnvironmentVariable("IsBuildMachine") == "1")
            {
				Status = SDKStatus.Valid;
            }
			return Status;
		}
	}
}