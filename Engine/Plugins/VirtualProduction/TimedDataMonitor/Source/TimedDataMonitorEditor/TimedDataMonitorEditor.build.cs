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
				"SlateCore",
				"Slate",
				"TimedDataMonitor",
				"TimeManagement",
				"TimeManagementEditor",
				"WorkspaceMenuStructure",
			});
	}
}
