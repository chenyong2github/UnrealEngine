// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Virtualization : ModuleRules
{
	public Virtualization(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("CoreUObject");

		//  Dependency for DDC2
		PrivateDependencyModuleNames.Add("DerivedDataCache");

		// Dependencies for the Jupiter service backend
		PrivateDependencyModuleNames.AddRange(new string[] { "SSL", "Json", "SourceControl" });

		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
