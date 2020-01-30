// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OodleHandlerComponent : ModuleRules
{
    public OodleHandlerComponent(ReadOnlyTargetRules Target) : base(Target)
    {
		ShortName = "OodleHC";

		PrivateIncludePaths.Add("OodleHandlerComponent/Private");

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"PacketHandler",
				"Core",
				"CoreUObject",
				"NetCore",
				"Engine",
                "Analytics"
			});


		if (Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Win32)
		{
			// this is needed to hunt down the DLL in the binaries directory for running unstaged
			PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Projects",
			});
		}

		string OodleNotForLicenseesNetBuildFile = System.IO.Path.Combine(Target.UEThirdPartySourceDirectory,
			"NotForLicensees/RadGames/OodleNet/OodleNet.Build.cs");
		string OodleLicenseesNetBuildFile = System.IO.Path.Combine(Target.UEThirdPartySourceDirectory,
			"RadGames/OodleNet/OodleNet.Build.cs");

		string OodleNotForLicenseesDataBuildFile = System.IO.Path.Combine(Target.UEThirdPartySourceDirectory,
			"NotForLicensees/RadGames/OodleData/OodleData.Build.cs");
		string OodleLicenseesDataBuildFile = System.IO.Path.Combine(Target.UEThirdPartySourceDirectory,
			"RadGames/OodleData/OodleData.Build.cs");

		bool bHaveNFLOodleNetSDK = System.IO.File.Exists(OodleNotForLicenseesNetBuildFile);
		bool bHaveLicenseesOodleNetSDK = System.IO.File.Exists(OodleLicenseesNetBuildFile);
		bool bHaveNFLOodleDataSDK = System.IO.File.Exists(OodleNotForLicenseesDataBuildFile);
		bool bHaveLicenseesOodleDataSDK = System.IO.File.Exists(OodleLicenseesDataBuildFile);

		// if we are loading from NFL, save binary in NFL
		if (bHaveNFLOodleDataSDK || bHaveNFLOodleNetSDK)
		{
			BinariesSubFolder = "NotForLicensees";
		}

		if (bHaveNFLOodleNetSDK || bHaveLicenseesOodleNetSDK)
		{
	        AddEngineThirdPartyPrivateStaticDependencies(Target, "OodleNet");
	        PublicIncludePathModuleNames.Add("OodleNet");
			PublicDefinitions.Add( "HAS_OODLE_NET_SDK=1" );

			// data SDK is optional
			if (bHaveNFLOodleDataSDK || bHaveLicenseesOodleDataSDK)
			{
				AddEngineThirdPartyPrivateStaticDependencies(Target, "OodleData");
				PublicIncludePathModuleNames.Add("OodleData");
				PublicDefinitions.Add("HAS_OODLE_DATA_SDK=1");
			}
			else
			{
				PublicDefinitions.Add("HAS_OODLE_DATA_SDK=0");
			}

		}
		else
		{
			PublicDefinitions.Add("HAS_OODLE_NET_SDK=0");
			PublicDefinitions.Add("HAS_OODLE_DATA_SDK=0");
		}
	}
}