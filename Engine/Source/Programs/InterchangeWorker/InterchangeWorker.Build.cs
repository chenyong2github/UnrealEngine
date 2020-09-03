// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class InterchangeWorker : ModuleRules
{
	public InterchangeWorker(ReadOnlyTargetRules Target) : base(Target)
	{

		PublicIncludePaths.Add("Runtime/Launch/Public");
		PrivateIncludePaths.Add("Runtime/Launch/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"ApplicationCore",
				"Core",
				"InterchangeDispatcher",
				"Json",
				"Projects",
				"Sockets",
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("InterchangeFbxParser");
		}

		PublicDelayLoadDLLs.Add("kernel_io.dll");
	}
}
