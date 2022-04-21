// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealBuildTool
{
	internal partial class MacPlatformSDK : ApplePlatformSDK
	{
		protected override void GetValidSoftwareVersionRange(out string MinVersion, out string? MaxVersion)
		{
			MinVersion = "10.15.7";
			MaxVersion = null;
		}
	}
}