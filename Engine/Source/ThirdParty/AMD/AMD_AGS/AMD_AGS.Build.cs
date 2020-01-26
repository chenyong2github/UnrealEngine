// Copyright Epic Games, Inc. All Rights Reserved.
using UnrealBuildTool;
using System.IO;

public class AMD_AGS : ModuleRules
{
	public AMD_AGS(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		string AmdAgsPath = Target.UEThirdPartySourceDirectory + "AMD/AMD_AGS/";
		PublicSystemIncludePaths.Add(AmdAgsPath + "inc/");

		if ((Target.Platform == UnrealTargetPlatform.Win64) ||
			(Target.Platform == UnrealTargetPlatform.Win32))
		{
			string AmdApiLibPath = AmdAgsPath + "lib/VS2017";

			if (Target.Platform == UnrealTargetPlatform.Win64)
			{
				string LibraryName = "amd_ags_x64_2017_MD.lib";
				PublicAdditionalLibraries.Add(Path.Combine(AmdApiLibPath, LibraryName));
			}
			else if (Target.Platform == UnrealTargetPlatform.Win32)
			{
				string LibraryName = "amd_ags_x86_2017_MD.lib";
				PublicAdditionalLibraries.Add(Path.Combine(AmdApiLibPath, LibraryName));
			}
		}
	}
}

