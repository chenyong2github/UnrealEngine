// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class Virtualization : ModuleRules
{
	public Virtualization(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.Add("Core");

		// Remove this when we move the rest of the Virtualization code to this module
		PrivateDependencyModuleNames.Add("CoreUObject"); 

		// Dependencies for the Jupiter service backend
		PrivateDependencyModuleNames.AddRange(new string[] { "SSL", "Json", "SourceControl" });

		AddEngineThirdPartyPrivateStaticDependencies(Target, "libcurl");
		AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");
	}
}
