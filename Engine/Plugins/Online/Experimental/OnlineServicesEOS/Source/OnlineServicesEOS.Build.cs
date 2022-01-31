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
				"CoreOnline",
				"CoreUObject",
				"EOSSDK",
				"EOSShared"
			}
		);

		if (Target.bCompileAgainstEngine)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"SocketSubsystemEOS",
					"Sockets"
				}
			);
		}
	}
}
