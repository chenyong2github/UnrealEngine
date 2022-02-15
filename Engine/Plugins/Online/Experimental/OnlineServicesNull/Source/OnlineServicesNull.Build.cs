// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OnlineServicesNull : ModuleRules
{
	public OnlineServicesNull(ReadOnlyTargetRules Target) : base(Target)
    {
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreOnline",
				"Sockets",
				"OnlineServicesInterface",
				"OnlineServicesCommon"
			}
		);

		if (Target.bCompileAgainstEngine)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"OnlineSubsystem",
					"OnlineSubsystemUtils"
				}
			);
		}
	}
}
