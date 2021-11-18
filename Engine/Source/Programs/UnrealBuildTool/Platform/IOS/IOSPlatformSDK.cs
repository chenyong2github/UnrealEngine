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
	internal class IOSPlatformSDK : ApplePlatformSDK
	{
		public override void GetValidSoftwareVersionRange(out string MinVersion, out string? MaxVersion)
		{
			// what is our min IOS version?
			MinVersion = "12.0";
			MaxVersion = null;
		}
	}
}
