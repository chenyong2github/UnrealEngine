// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;
using System;

public class AMD_AGS : ModuleRules
{
	public AMD_AGS(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string AmdAgsPath = Target.UEThirdPartySourceDirectory + "AMD/AMD_AGS/";
		PublicSystemIncludePaths.Add(AmdAgsPath + "inc/");

		if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && Target.Architecture.IndexOf("arm", StringComparison.OrdinalIgnoreCase) == -1)
		{
			string AmdApiLibPath = AmdAgsPath + "lib/VS2017";

			string LibraryName = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
				? "amd_ags_x64_2017_MDd.lib"
				: "amd_ags_x64_2017_MD.lib";

			PublicAdditionalLibraries.Add(Path.Combine(AmdApiLibPath, LibraryName));

			PublicDefinitions.Add("WITH_AMD_AGS=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_AMD_AGS=0");
		}
	}
}

