// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ConcertSharedSlate : ModuleRules
	{
		public ConcertSharedSlate(ReadOnlyTargetRules Target) : base(Target)
		{
			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Slate",
					"EditorStyle",
					"Concert"
				}
			);

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"SlateCore",
					"InputCore",
					"Projects",
					"Concert",
					"ConcertTransport",
					"ConcertSyncClient",
					"ConcertSyncCore",
					"UndoHistory"
				}
			);
		}
	}
}
