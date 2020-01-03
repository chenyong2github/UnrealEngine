// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MultiUserClient : ModuleRules
	{
		public MultiUserClient(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"Concert",
					"ConcertSyncClient",
					"ConcertSyncCore",
					"ConcertSyncUI",
					"ConcertTransport",
					"DesktopPlatform",
					"EditorStyle",
					"InputCore",
					"Projects",
					"MessageLog",
					"Slate",
					"SlateCore",
					"SourceControl",
				}
			);

			if (Target.bBuildEditor)
			{
				PrivateDependencyModuleNames.AddRange(
					new string[]
					{
						"UnrealEd",
						"Sequencer",
						"WorkspaceMenuStructure",
					}
				);
			}
		}
	}
}
