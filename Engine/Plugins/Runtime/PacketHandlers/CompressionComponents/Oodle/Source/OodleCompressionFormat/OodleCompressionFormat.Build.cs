// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OodleCompressionFormat : ModuleRules
{
	public OodleCompressionFormat(ReadOnlyTargetRules Target) : base(Target)
	{
		// @todo oodle: Clean this up with the handler component?

		ShortName = "OodleFormat";
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"PacketHandler",
				"Core",
				"CoreUObject",
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

		// Check the NotForLicensees folder first
		string OodleNotForLicenseesBuildFile = System.IO.Path.Combine(Target.UEThirdPartySourceDirectory, 
			"NotForLicensees/RadGames/OodleData/OodleData.Build.cs");
		string OodleLicenseesBuildFile = System.IO.Path.Combine(Target.UEThirdPartySourceDirectory,
			"RadGames/OodleData/OodleData.Build.cs");

		bool bHaveNFLOodleSDK = System.IO.File.Exists(OodleNotForLicenseesBuildFile);
		bool bHaveLicenseesOodleSDK = System.IO.File.Exists(OodleLicenseesBuildFile);

		// if we are loading from NFL, save binary in NFL
		if (bHaveNFLOodleSDK)
		{
			BinariesSubFolder = "NotForLicensees";
		}

		if (bHaveNFLOodleSDK || bHaveLicenseesOodleSDK)
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
}