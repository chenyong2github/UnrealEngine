// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class OpenSubdiv : ModuleRules
{
	public OpenSubdiv(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		// Compile and link with OpenSubDiv
        string OpenSubdivPath = Target.UEThirdPartySourceDirectory + "OpenSubdiv/3.2.0";

		PublicIncludePaths.Add( OpenSubdivPath + "/opensubdiv" );

		// @todo mesheditor subdiv: Support other platforms, 32-bit Windows, and older/newer compiler toolchains
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
            string LibFolder = "/lib/Win64/VS2015";
            if (LibFolder != "")
            {
                bool bDebug = (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT);
                string ConfigFolder = bDebug ? "/Debug" : "/RelWithDebInfo";

                PublicAdditionalLibraries.Add(OpenSubdivPath + LibFolder + ConfigFolder + "/osdCPU.lib");
            }
        }
		else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
			// @todo: build Mac libraries
//            string LibFolder = "/lib/Mac";
//            string ConfigFolder = bDebug ? "" : "";
//
//            PublicAdditionalLibraries.Add(OpenSubdivPath + LibFolder + ConfigFolder + "libosdCPU.a");
        }
		else if (Target.Platform == UnrealTargetPlatform.Linux)
        {
			// @todo: build Linux libraries
//            string LibFolder = "/lib/Linux/" + Target.Architecture;
//
//            PublicAdditionalLibraries.Add(OpenSubdivPath + LibFolder + "libosdCPU.a");
        }
    }
}