// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class OnlineServicesEOS : ModuleRules
{
	public OnlineServicesEOS(ReadOnlyTargetRules Target) : base(Target)
    {
		PublicDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"OnlineServicesInterface",
				"OnlineServicesCommon",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"EOSSDK",
				"EOSShared",
			}
		);
	}
}
