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
				"CoreUObject",
				"InterchangeDispatcher",
				"Json",
				"Projects",
				"Sockets",
			}
		);

		if (Target.Platform == UnrealTargetPlatform.Win64 ||
			Target.Platform == UnrealTargetPlatform.Linux ||
			Target.Platform == UnrealTargetPlatform.Mac)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"InterchangeCore",
					"InterchangeFbxParser",
					"InterchangeNodes"
				}
			);

			AddEngineThirdPartyPrivateStaticDependencies(Target,
				new string[]
				{
					"FBX"
				}
			);
		}
	}
}
