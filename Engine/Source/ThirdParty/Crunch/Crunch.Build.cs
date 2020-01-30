// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Crunch : ModuleRules
{
	public Crunch(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        string BasePath = Target.UEThirdPartySourceDirectory + "Crunch/";
        PublicSystemIncludePaths.Add(BasePath + "include");

        if (Target.Type == TargetType.Editor)
        {
            // link with lib to allow encoding
            string LibPath = BasePath + "Lib/";

            if (Target.Platform == UnrealTargetPlatform.Win32 ||
                Target.Platform == UnrealTargetPlatform.Win64)
            {
                LibPath += (Target.Platform == UnrealTargetPlatform.Win64) ? "Win64/" : "Win32/";
                PublicAdditionalLibraries.Add(LibPath + "crnlib.lib");
            }
        }
    }
}
