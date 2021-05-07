// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DisplayClusterMessageInterception : ModuleRules
{
	public DisplayClusterMessageInterception(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Concert",
				"ConcertSyncClient",
				"ConcertSyncCore",
				"ConcertTransport",
				"DisplayCluster",
				"Engine",
				"Messaging"
			});
	}
}
