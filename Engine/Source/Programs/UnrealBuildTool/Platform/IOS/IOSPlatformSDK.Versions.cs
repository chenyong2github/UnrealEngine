// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.Extensions.Logging;

namespace UnrealBuildTool
{
	internal class IOSPlatformSDK : ApplePlatformSDK
	{
		public IOSPlatformSDK(ILogger Logger)
			: base(Logger)
		{
		}

		protected override void GetValidSoftwareVersionRange(out string MinVersion, out string? MaxVersion)
		{
			// what is our min IOS version?
			MinVersion = "14.0";
			MaxVersion = null;
		}
	}
}
