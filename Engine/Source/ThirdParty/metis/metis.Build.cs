// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class metis : ModuleRules
{
	public metis(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

        string MetisPath = Target.UEThirdPartySourceDirectory + "metis/5.1.0";

		PublicIncludePaths.Add(MetisPath + "/include" );

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
            string LibFolder = "/libmetis";
            if (LibFolder != "")
            {
                bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug);// && Target.bDebugBuildsActuallyUseDebugCRT);
                string ConfigFolder = bDebug ? "/Debug" : "/Release";

                PublicAdditionalLibraries.Add(MetisPath + LibFolder + ConfigFolder + "/metis.lib");
            }
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
			// @todo: build Mac libraries
        }
		else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
			// @todo: build Linux libraries
        }
    }
}