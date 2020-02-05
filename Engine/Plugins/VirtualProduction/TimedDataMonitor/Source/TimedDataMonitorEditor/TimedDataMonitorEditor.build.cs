// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TimedDataMonitorEditor : ModuleRules
{
	public TimedDataMonitorEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PrivateDependencyModuleNames.AddRange(
			new string[] {
				"Core",
				"CoreUObject",
				"Engine",
				"EditorStyle",
				"InputCore",
				"MessageLog",
				"Settings",
				"SlateCore",
				"Slate",
				"TimedDataMonitor",
				"TimeManagement",
				"TimeManagementEditor",
				"UnrealEd",
				"WorkspaceMenuStructure",
			});
	}
}
