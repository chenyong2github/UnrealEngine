// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class RenderCore : ModuleRules
{
	public RenderCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("../Shaders/Shared");

		PublicDependencyModuleNames.AddRange(new string[] { "RHI" });

		PrivateIncludePathModuleNames.AddRange(new string[] { "TargetPlatform" });

		// JSON is used for the asset info in the shader library and dumping out frames.
		PrivateDependencyModuleNames.Add("Json");

		PrivateDependencyModuleNames.Add("BuildSettings");

		if (Target.bBuildEditor == true)
        {
			DynamicallyLoadedModuleNames.Add("TargetPlatform");
			// UObjects are used to produce the full path of the asset by which the shaders are identified
			PrivateDependencyModuleNames.Add("CoreUObject");
		}

        PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Projects", "RHI", "ApplicationCore", "TraceLog", "CookOnTheFly" });

        PrivateIncludePathModuleNames.AddRange(new string[] { "DerivedDataCache" });
		
		// Added in Dev-VT, still needed?
		PrivateIncludePathModuleNames.AddRange(new string[] { "TargetPlatform" });
    }
}
