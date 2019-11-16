// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class DisasterRecoveryClient : ModuleRules
	{
		public DisasterRecoveryClient(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Concert",
					"ConcertSyncClient",
					"ConcertSyncCore",
					"ConcertSyncUI",
					"ConcertTransport",
					"UnrealEd",
					"EditorStyle",
					"InputCore",
					"Serialization",
					"Slate",
					"SlateCore",
				}
			);
		}
	}
}
