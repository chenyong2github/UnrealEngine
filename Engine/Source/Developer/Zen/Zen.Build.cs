// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Zen : ModuleRules
{
	public Zen(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "Sockets", "SSL", "Json" });
		PrivateIncludePathModuleNames.Add("DesktopPlatform");
		
		PublicDependencyModuleNames.AddRange(
			new string[] {		
				"Analytics",
			}
		);

		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

		UnsafeTypeCastWarningLevel = WarningLevel.Error;
	}
}
