// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool
{
	internal class IOSPlatformSDK : ApplePlatformSDK
	{
		protected override void GetValidSoftwareVersionRange(out string MinVersion, out string? MaxVersion)
		{
			// what is our min IOS version?
			MinVersion = "12.0";
			MaxVersion = null;
		}
	}
}
