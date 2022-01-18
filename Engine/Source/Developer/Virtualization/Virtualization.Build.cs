// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Virtualization : ModuleRules
{
	public Virtualization(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");
		PrivateDependencyModuleNames.Add("CoreUObject");
		PrivateDependencyModuleNames.Add("MessageLog");

		// Dependency for DDC2
		PrivateDependencyModuleNames.Add("DerivedDataCache");

		// Dependency for source control backend
		PrivateDependencyModuleNames.Add("SourceControl");

		// Dependencies for the Horde Storage backend
		PrivateDependencyModuleNames.AddRange(new string[] { "SSL", "Json" });

		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
