// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class ConcertServerUI : ModuleRules
	{
		public ConcertServerUI(ReadOnlyTargetRules Target) : base(Target)
		{
			// Make the linking path shorter (to prevent busting the Windows limit) when linking ConcertServerUI.lib against an executable that have a long name.
			ShortName = "CncrtSvrUI";

			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"Core",
					"CoreUObject",
					"Concert",
					"ConcertSharedSlate",
					"ConcertSyncCore",
					"ConcertTransport",
					"EditorStyle",			// Needed so OutputLog module to work correctly
					"InputCore",
					"OutputLog",
					"Projects",
					"Slate",
					"SlateCore",
					"StandaloneRenderer",	
					"ToolWidgets",
					"WorkspaceMenuStructure" // Needed so OutputLog module to work correctly
				}
			);
			
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"ConcertSyncServer",
				}
			);
			
			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					"ConcertSyncServer"
				});
		}
	}
}
