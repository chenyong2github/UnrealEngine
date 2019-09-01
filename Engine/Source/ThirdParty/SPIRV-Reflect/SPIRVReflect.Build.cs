// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class SPIRVReflect : ModuleRules
{
	public SPIRVReflect(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		PublicSystemIncludePaths.Add(Target.UEThirdPartySourceDirectory + "SPIRV-Reflect/SPIRV-Reflect");

		string LibPath = Target.UEThirdPartySourceDirectory + "SPIRV-Reflect/lib/";
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
			{
				PublicAdditionalLibraries.Add(LibPath + "Mac/libspirv-reflectd.a");
			}
			else
			{
				PublicAdditionalLibraries.Add(LibPath + "Mac/libspirv-reflect.a");
			}
		}
        else if (Target.Platform == UnrealTargetPlatform.Win64)
        {
			LibPath = LibPath + (Target.Platform == UnrealTargetPlatform.Win32 ? "Win32/" : "Win64/");
            LibPath = LibPath + "VS2017/";

            if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.bDebugBuildsActuallyUseDebugCRT)
            {
                PublicAdditionalLibraries.Add(LibPath + "SPIRV-Reflectd.lib");
            }
            else
            {
                PublicAdditionalLibraries.Add(LibPath + "SPIRV-Reflect.lib");
            }
        }
        else
		{
			string Err = string.Format("Attempt to build against SPIRV-Reflect on unsupported platform {0}", Target.Platform);
			System.Console.WriteLine(Err);
			throw new BuildException(Err);
		}
	}
}

