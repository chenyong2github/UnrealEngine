// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DerivedDataCache : ModuleRules
{
	public DerivedDataCache(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");

		// Dependencies for "S3" and "HTTP" backends
		PrivateDependencyModuleNames.AddRange(new string[] { "SSL", "Json" });
		PrivateIncludePathModuleNames.Add("DesktopPlatform");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

		// Internal (NotForLicensees) module
		var DDCUtilsModule = Path.Combine(EngineDirectory, "Restricted/NotForLicensees/Source/Developer/DDCUtils/DDCUtils.Build.cs");
		if (File.Exists(DDCUtilsModule))
		{
			DynamicallyLoadedModuleNames.Add("DDCUtils");
		}

		// Platform-specific opt-in
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDefinitions.Add("WITH_HTTP_DDC_BACKEND=1");
			PrivateDefinitions.Add("WITH_S3_DDC_BACKEND=1");
		}
	}
}
