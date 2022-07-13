// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ReplicationSystemTest : ModuleRules
{
	public ReplicationSystemTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePaths.Add("Runtime/Launch/Public");

		// For LaunchEngineLoop.cpp include
		PrivateIncludePaths.Add("Runtime/Launch/Private");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
 				"ApplicationCore",
 				"AutomationController",
 				"AutomationWorker",
                "Core",
				"Projects",
				"DerivedDataCache",
				"Engine",
				"HeadMountedDisplay",
				"InstallBundleManager",
                "MediaUtils",
				"MRMesh",
				"MoviePlayer",
				"MoviePlayerProxy",
				"PreLoadScreen",
				"ProfilerService",
				"ReplicationSystemTestPlugin",
				"SessionServices",
				"SlateNullRenderer",
				"SlateRHIRenderer",
				"TaskGraph",
 			}
		);
	}
}
