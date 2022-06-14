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
	internal partial class ApplePlatformSDK : UEBuildPlatformSDK
	{
		public override string GetMainVersion()
		{
			return "13.3";
		}

		protected override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			if (RuntimePlatform.IsMac)
			{
				MinVersion = "12.4.0";
				MaxVersion = "14.9.9";
			}
			else
			{
				// @todo turnkey: these are MobileDevice .dll versions in Windows - to get the iTunes app version (12.3.4.1 etc) would need to hunt down the .exe
				MinVersion = "1100.0.0.0";
				MaxVersion = "1499.0";
			}
		}

		protected override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}
	}
}
