// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;

namespace UnrealBuildTool
{
	/////////////////////////////////////////////////////////////////////////////////////
	// If you are looking for any version numbers not listed here, see Linux_SDK.json
	/////////////////////////////////////////////////////////////////////////////////////

	partial class LinuxPlatformSDK : UEBuildPlatformSDK
	{
		protected override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}
	}
}
