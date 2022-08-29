// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Virtualization : ModuleRules
{
	public Virtualization(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"ApplicationCore",
				"DerivedDataCache",
				"MessageLog",
				"Projects",
				"SourceControl"
			});

		// The dependencies below this point are for FHttpBackend
		// and can be removed when it is removed
		PrivateDependencyModuleNames.AddRange(new string[] { "SSL", "Json" });

		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
