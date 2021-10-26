// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Zen : ModuleRules
{
	public Zen(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");

		// Dependencies for "S3" and "HTTP" backends
		PrivateDependencyModuleNames.AddRange(new string[] { "SSL", "Json" });
		PrivateIncludePathModuleNames.Add("DesktopPlatform");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
