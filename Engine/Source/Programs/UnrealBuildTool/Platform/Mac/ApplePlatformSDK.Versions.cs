// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.Core;


using UnrealBuildBase;

namespace UnrealBuildTool
{
	/////////////////////////////////////////////////////////////////////////////////////
	// If you are looking for any version numbers not listed here, see Apple_SDK.json
	/////////////////////////////////////////////////////////////////////////////////////

	internal partial class ApplePlatformSDK : UEBuildPlatformSDK
	{
		protected override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			MinVersion = MaxVersion = null;
		}

		/// <summary>
		/// The minimum macOS SDK version that a dynamic library can be built with
		/// </summary>
		public virtual Version MinimumDynamicLibSDKVersion => new Version("12.1");      // SDK used in Xcode13.2.1
	}
}
