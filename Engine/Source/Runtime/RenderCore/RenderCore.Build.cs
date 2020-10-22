// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderCore : ModuleRules
{
	public RenderCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] { "RHI" });
		
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.Add("TargetPlatform");
			// PakFileUtitilities due to file open order usage by the shader library
			PrivateDependencyModuleNames.Add("PakFileUtilities");
			// JSON is used for the asset info in the shader library
			PrivateDependencyModuleNames.Add("Json");
		}
		else
        {

            PrivateIncludePathModuleNames.AddRange(new string[] { "TargetPlatform" });

        }

        PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Projects", "RHI", "ApplicationCore", "TraceLog" });

        PrivateIncludePathModuleNames.AddRange(new string[] { "DerivedDataCache" });
		
		// Added in Dev-VT, still needed?
		PrivateIncludePathModuleNames.AddRange(new string[] { "TargetPlatform" });
    }
}
