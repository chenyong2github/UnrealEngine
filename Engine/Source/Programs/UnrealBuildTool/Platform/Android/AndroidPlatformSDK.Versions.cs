// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealBuildTool
{
	partial class AndroidPlatformSDK : UEBuildPlatformSDK
	{
		public override string GetMainVersion()
		{
			return "r21e";
		}
		
		public override string GetAutoSDKDirectoryForMainVersion()
		{
			return "-24";
		}

		protected override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			MinVersion = "r21a";
			MaxVersion = "r25";
		}

		protected override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}

		public override string GetPlatformSpecificVersion(string VersionType)
		{
			switch (VersionType.ToLower())
			{
				case "platforms": return "android-28";
				case "build-tools": return "28.0.3";
				case "cmake": return "3.10.2.4988404";
				case "ndk": return "21.4.7075529";
			}

			return "";
		}
	}
}
