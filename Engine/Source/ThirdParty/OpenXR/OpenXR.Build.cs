// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class OpenXR : ModuleRules
{
	public OpenXR(ReadOnlyTargetRules Target) : base(Target)
	{
		/** Mark the current version of the OpenXR SDK */
		string OpenXRVersion = "1_0";
		Type = ModuleType.External;

        string RootPath = Target.UEThirdPartySourceDirectory + "OpenXR";
        string LoaderPath = RootPath + "/loader";

        PublicSystemIncludePaths.Add(RootPath + "/include");

		if(Target.Platform == UnrealTargetPlatform.Win32)
		{
			PublicAdditionalLibraries.Add(LoaderPath + "/win32/" + String.Format("openxr_loader-{0}.lib", OpenXRVersion));

			PublicDelayLoadDLLs.Add(String.Format("openxr_loader-{0}.dll", OpenXRVersion));
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/OpenXR/win32/" + String.Format("openxr_loader-{0}.dll", OpenXRVersion));	
		}
        else if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PublicAdditionalLibraries.Add(LoaderPath + "/win64/" + String.Format("openxr_loader-{0}.lib", OpenXRVersion));

			PublicDelayLoadDLLs.Add(String.Format("openxr_loader-{0}.dll", OpenXRVersion));
			RuntimeDependencies.Add("$(EngineDir)/Binaries/ThirdParty/OpenXR/win64/" + String.Format("openxr_loader-{0}.dll", OpenXRVersion));			
        }
    }
}
