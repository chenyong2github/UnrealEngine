// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using EpicGames.Core;
using System.Text.RegularExpressions;
using Microsoft.Win32;
using System.Diagnostics;

///////////////////////////////////////////////////////////////////
// If you are looking for supported version numbers, look in the
// ApplePlatformSDK.Versions.cs file next to this file, and
// als Mac/IOSPlatformSDK.Versions.cs
///////////////////////////////////////////////////////////////////

namespace UnrealBuildTool
{
	internal partial class ApplePlatformSDK : UEBuildPlatformSDK
	{
		public override bool TryConvertVersionToInt(string? StringValue, out UInt64 OutValue, string? Hint)
		{
			OutValue = 0;

			if (StringValue == null)
			{
				return false;
			}

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

			return false;
		}

		protected override string? GetInstalledSDKVersion()
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