// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DatasmithCADWorker : ModuleRules
{
	public DatasmithCADWorker(ReadOnlyTargetRules Target) : base(Target)
	{

		PublicIncludePaths.Add("Runtime/Launch/Public");
		PrivateIncludePaths.Add("Runtime/Launch/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Projects",
				"ApplicationCore",
				"Sockets",
				"DatasmithDispatcher",
				"CADInterfaces",
				"CADTools",
			}
		);

		PublicDelayLoadDLLs.Add("kernel_io.dll");
	}
}
