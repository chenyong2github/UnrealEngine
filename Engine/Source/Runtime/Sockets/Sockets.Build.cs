// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Sockets : ModuleRules
{
	public Sockets(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateIncludePaths.Add("Runtime/Sockets/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
			});

		PublicDefinitions.Add("SOCKETS_PACKAGE=1");
	}
}
