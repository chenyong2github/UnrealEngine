// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderCore : ModuleRules
{
	public RenderCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PublicDependencyModuleNames.AddRange(new string[] { "RHI" });

		PrivateIncludePathModuleNames.AddRange(new string[] { "TargetPlatform" });

		if (Target.bBuildEditor == true)
        {
			DynamicallyLoadedModuleNames.Add("TargetPlatform");
			// PakFileUtitilities due to file open order usage by the shader library
			PrivateDependencyModuleNames.Add("PakFileUtilities");
			// JSON is used for the asset info in the shader library
			PrivateDependencyModuleNames.Add("Json");
			// UObjects are used to produce the full path of the asset by which the shaders are identified
			PrivateDependencyModuleNames.Add("CoreUObject");
		}

        PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Projects", "RHI", "ApplicationCore", "TraceLog", "CookOnTheFly" });

        PrivateIncludePathModuleNames.AddRange(new string[] { "DerivedDataCache" });
		
		// Added in Dev-VT, still needed?
		PrivateIncludePathModuleNames.AddRange(new string[] { "TargetPlatform" });
    }
}
