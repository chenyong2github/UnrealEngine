// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealBuildTool
{
	partial class LinuxPlatformSDK : UEBuildPlatformSDK
	{
		public override string GetMainVersion()
		{
			return "v19_clang-11.0.1-centos7";
		}

		protected override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			// all that matters is the number after the v, according to TryConvertVersionToInt()
			MinVersion = "v10_clang-5.0.0-centos7";
			MaxVersion = "v19_clang-11.0.1-centos7";
		}

		protected override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}
	}
}
