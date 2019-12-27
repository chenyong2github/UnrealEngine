// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using UnrealBuildTool;

public class HoloLensTargetPlatform : ModuleRules
{
	private string ModulePath
	{
		get { return ModuleDirectory; }
	}
 
	private string ThirdPartyPath
	{
		get { return Path.GetFullPath( Path.Combine( ModulePath, "../ThirdParty" ) ); }
	}
	
	public HoloLensTargetPlatform(ReadOnlyTargetRules Target) : base(Target)
    {
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Settings",
                "EngineSettings",
                "TargetPlatform",
				"DesktopPlatform",
				"HoloLensDeviceDetector",
				"HTTP",
				"HoloLensPlatformEditor"
			}
		);

		PrivateIncludePathModuleNames.Add("Settings");

		// compile withEngine
		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.Add("Engine");
			PrivateIncludePathModuleNames.Add("TextureCompressor");
		}

		PublicSystemLibraries.Add("shlwapi.lib");
		
		string LibrariesPath = Path.Combine(ThirdPartyPath, "Lib", Target.WindowsPlatform.GetArchitectureSubpath());
				
		PublicAdditionalLibraries.Add(Path.Combine(LibrariesPath, "HoloLensBuildLib.lib"));
		
		PrivateIncludePaths.Add(Path.Combine(ThirdPartyPath, "Include/"));
	}
}
