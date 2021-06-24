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

		// TODO:  Use EOSShared module
		PrivateDefinitions.Add("WITH_EOS_SDK=0");
	}
}
