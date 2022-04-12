// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealBuildTool
{
	internal partial class MacPlatformSDK : ApplePlatformSDK
	{
		public override string GetMainVersion()
		{
			return "11.5";
		}

		protected override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
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

		protected override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}
	}
}